#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "libmap.h"
#include "move.h"
#include "tactics/goals.h"
#include "tactics/util.h"


struct libmap_config libmap_config = {
	.pick_mode = LMP_THRESHOLD,
	.pick_threshold = 0.7,
	.pick_epsilon = 10,

	.explore_p = 0.2,
	.prior = { .value = 0.5, .playouts = 1 },
	.tenuki_prior = { .value = 0.4, .playouts = 1 },

	.mq_merge_groups = true,
	.counterattack = LMC_DEFENSE | LMC_ATTACK | LMC_DEFENSE_ATTACK,
	.eval = LME_LVALUE,
};

void
libmap_setup(char *arg)
{
	if (!arg)
		return;

	char *optspec, *next = arg;
	while (*next) {
		optspec = next;
		next += strcspn(next, ":");
		if (*next) { *next++ = 0; } else { *next = 0; }

		char *optname = optspec;
		char *optval = strchr(optspec, '=');
		if (optval) *optval++ = 0;

		if (!strcasecmp(optname, "pick_mode") && optval) {
			if (!strcasecmp(optval, "threshold")) {
				libmap_config.pick_mode = LMP_THRESHOLD;
			} else if (!strcasecmp(optval, "ucb")) {
				libmap_config.pick_mode = LMP_UCB;
			} else {
				fprintf(stderr, "Invalid libmap:pick_mode value %s\n", optval);
				exit(1);
			}

		} else if (!strcasecmp(optname, "pick_threshold") && optval) {
			libmap_config.pick_threshold = atof(optval);
		} else if (!strcasecmp(optname, "pick_epsilon") && optval) {
			libmap_config.pick_epsilon = atoi(optval);
		} else if (!strcasecmp(optname, "avoid_bad")) {
			libmap_config.avoid_bad = !optval || atoi(optval);

		} else if (!strcasecmp(optname, "explore_p") && optval) {
			libmap_config.explore_p = atof(optval);
		} else if (!strcasecmp(optname, "prior") && optval && strchr(optval, 'x')) {
			libmap_config.prior.value = atof(optval);
			optval += strcspn(optval, "x") + 1;
			libmap_config.prior.playouts = atoi(optval);
		} else if (!strcasecmp(optname, "tenuki_prior") && optval && strchr(optval, 'x')) {
			libmap_config.tenuki_prior.value = atof(optval);
			optval += strcspn(optval, "x") + 1;
			libmap_config.tenuki_prior.playouts = atoi(optval);

		} else if (!strcasecmp(optname, "mq_merge_groups")) {
			libmap_config.mq_merge_groups = !optval || atoi(optval);
		} else if (!strcasecmp(optname, "counterattack") && optval) {
			/* Combination of letters d, a, x (both), these kinds
			 * of hashes are going to be recorded. */
			/* Note that using multiple letters makes no sense
			 * if mq_merge_groups is set. */
			libmap_config.counterattack = 0;
			if (strchr(optval, 'd'))
				libmap_config.counterattack |= LMC_DEFENSE;
			if (strchr(optval, 'a'))
				libmap_config.counterattack |= LMC_ATTACK;
			if (strchr(optval, 'x'))
				libmap_config.counterattack |= LMC_DEFENSE_ATTACK;
		} else if (!strcasecmp(optname, "eval") && optval) {
			if (!strcasecmp(optval, "local")) {
				libmap_config.eval = LME_LOCAL;
			} else if (!strcasecmp(optval, "lvalue")) {
				libmap_config.eval = LME_LVALUE;
			} else if (!strcasecmp(optval, "global")) {
				libmap_config.eval = LME_GLOBAL;
			} else {
				fprintf(stderr, "Invalid libmap:eval value %s\n", optval);
				exit(1);
			}
		} else if (!strcasecmp(optname, "tenuki")) {
			libmap_config.tenuki = !optval || atoi(optval);
		} else {
			fprintf(stderr, "Invalid libmap argument %s or missing value\n", optname);
			exit(1);
		}
	}
}


struct libmap_hash *
libmap_init(struct board *b)
{
	struct libmap_hash *lm = calloc2(1, sizeof(*lm));
	lm->b = b;
	b->libmap = lm;
	lm->refcount = 1;

	lm->groups[0] = calloc2(board_size2(b), sizeof(*lm->groups[0]));
	lm->groups[1] = calloc2(board_size2(b), sizeof(*lm->groups[1]));
	for (group_t g = 1; g < board_size2(b); g++) // foreach_group
		if (group_at(b, g) == g)
			libmap_group_init(lm, b, g, board_at(b, g));

	return lm;
}

void
libmap_put(struct libmap_hash *lm)
{
	if (__sync_sub_and_fetch(&lm->refcount, 1) > 0)
		return;
	for (group_t g = 0; g < board_size2(lm->b); g++) {
		if (lm->groups[0][g])
			free(lm->groups[0][g]);
		if (lm->groups[1][g])
			free(lm->groups[1][g]);
	}
	free(lm->groups[0]);
	free(lm->groups[1]);
	free(lm);
}

void
libmap_group_init(struct libmap_hash *lm, struct board *b, group_t g, enum stone color)
{
	assert(color == S_BLACK || color == S_WHITE);
	if (lm->groups[color - 1][g])
		return;

	struct libmap_group *lmg = calloc2(1, sizeof(*lmg));
	lmg->group = g;
	lmg->color = color;
	lm->groups[color - 1][g] = lmg;
}


void
libmap_queue_process(struct board *b, enum stone winner)
{
	struct libmap_mq *lmqueue = b->lmqueue;
	assert(lmqueue->mq.moves <= MQL);
	for (unsigned int i = 0; i < lmqueue->mq.moves; i++) {
		struct libmap_move_groupinfo *gi = &lmqueue->gi[i];
		struct move m = { .coord = lmqueue->mq.move[i], .color = lmqueue->color[i] };
		struct libmap_group *lg = b->libmap->groups[gi->color - 1][gi->group];
		if (!lg) continue;
		floating_t val;
		if (libmap_config.eval == LME_LOCAL || libmap_config.eval == LME_LVALUE) {
			val = board_local_value(libmap_config.eval == LME_LVALUE, b, gi->group, gi->goal);

		} else { assert(libmap_config.eval == LME_GLOBAL);
			val = winner == gi->goal ? 1.0 : 0.0;
		}
		libmap_add_result(b->libmap, lg, gi->hash, m, val, 1);
	}
	lmqueue->mq.moves = 0;
}

void
libmap_add_result(struct libmap_hash *lm, struct libmap_group *lg, hash_t hash, struct move move,
                  floating_t result, int playouts)
{
	/* If hash line is full, replacement strategy is naive - pick the
	 * move with minimum move[0].stats.playouts; resolve each tie
	 * randomly. */
	unsigned int min_playouts = INT_MAX; hash_t min_hash = hash;
	hash_t ih;
	for (ih = hash; lg->hash[ih & libmap_hash_mask].hash != hash; ih++) {
		// fprintf(stderr, "%"PRIhash": check %"PRIhash" (%d)\n", hash & libmap_hash_mask, ih & libmap_hash_mask, lg->hash[ih & libmap_hash_mask].moves);
		if (lg->hash[ih & libmap_hash_mask].moves == 0) {
			lg->hash[ih & libmap_hash_mask].hash = hash;
			break;
		}
		if (ih >= hash + libmap_hash_maxline) {
			/* Snatch the least used bucket. */
			ih = min_hash;
			// fprintf(stderr, "clear %"PRIhash"\n", ih & libmap_hash_mask);
			memset(&lg->hash[ih & libmap_hash_mask], 0, sizeof(lg->hash[0]));
			lg->hash[ih & libmap_hash_mask].hash = hash;
			break;
		}

		/* Keep track of least used bucket. */
		assert(lg->hash[ih & libmap_hash_mask].moves > 0);
		unsigned int hp = lg->hash[ih & libmap_hash_mask].move[0].stats.playouts;
		if (hp < min_playouts || (hp == min_playouts && fast_random(2))) {
			min_playouts = hp;
			min_hash = ih;
		}
	}

	// fprintf(stderr, "%"PRIhash": use %"PRIhash" (%d)\n", hash & libmap_hash_mask, ih & libmap_hash_mask, lg->hash[ih & libmap_hash_mask].moves);
	struct libmap_context *lc = &lg->hash[ih & libmap_hash_mask];
	lc->visits++;

	for (int i = 0; i < lc->moves; i++) {
		if (lc->move[i].move.coord == move.coord
		    && lc->move[i].move.color == move.color) {
			stats_add_result(&lc->move[i].stats, result, playouts);
			return;
		}
	}

	int moves = lc->moves; // to preserve atomicity
	if (moves >= GROUP_REFILL_LIBS) {
		if (DEBUGL(5))
			fprintf(stderr, "(%s) too many libs\n", coord2sstr(move.coord, lm->b));
		return;
	}
	lc->move[moves].move = move;
	stats_add_result(&lc->move[moves].stats, result, playouts);
	lc->moves = ++moves;
}

struct move_stats
libmap_board_move_stats(struct libmap_hash *lm, struct board *b, struct move move)
{
	struct move_stats tot = { .playouts = 0, .value = 0 };
	if (is_pass(move.coord))
		return tot;
	assert(board_at(b, move.coord) != S_OFFBOARD);

	neighboring_groups_list(b, board_at(b, c) == S_BLACK || board_at(b, c) == S_WHITE,
			move.coord, groups, groups_n, groupsbycolor_xxunused);
	for (int i = 0; i < groups_n; i++) {
		struct libmap_group *lg = lm->groups[board_at(b, groups[i]) - 1][groups[i]];
		if (!lg) continue;
		hash_t hash = group_to_libmap(b, groups[i]);
		struct move_stats *lp = libmap_move_stats(b->libmap, lg, hash, move);
		if (!lp) continue;
		stats_merge(&tot, lp);
	}

	return tot;
}