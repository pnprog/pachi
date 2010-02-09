#ifndef ZZGO_TACTICS_H
#define ZZGO_TACTICS_H

/* Advanced tactical checks non-essential to the board implementation. */

#include "board.h"

struct move_queue;

/* Check if this move is undesirable self-atari (resulting group would have
 * only single liberty and not capture anything; ko is allowed); we mostly
 * want to avoid these moves. The function actually does a rather elaborate
 * tactical check, allowing self-atari moves that are nakade, eye falsification
 * or throw-ins. */
static bool is_bad_selfatari(struct board *b, enum stone color, coord_t to);

/* Check if escaping on this liberty by given group in atari would play out
 * a simple ladder. */
/* Two ways of ladder reading can be enabled separately; simple first-line
 * ladders and trivial middle-board ladders. */
static bool is_ladder(struct board *b, coord_t coord, group_t laddered,
                      bool border_ladders, bool middle_ladders);

/* Checks if there are any stones in n-vincinity of coord. */
bool board_stone_radar(struct board *b, coord_t coord, int distance);

/* Measure various distances on the board: */
/* Distance from the edge; on edge returns 0. */
static int coord_edge_distance(coord_t c, struct board *b);
/* Distance of two points in gridcular metric - this metric defines
 * circle-like structures on the square grid. */
static int coord_gridcular_distance(coord_t c1, coord_t c2, struct board *b);

/* Construct a "common fate graph" from given coordinate; that is, a weighted
 * graph of intersections where edges between all neighbors have weight 1,
 * but edges between neighbors of same color have weight 0. Thus, this is
 * "stone chain" metric in a sense. */
/* The output are distanes from start stored in given [board_size2()] array;
 * intersections further away than maxdist have all distance maxdist+1 set. */
void cfg_distances(struct board *b, coord_t start, int *distances, int maxdist);

/* Compute an extra komi describing the "effective handicap" black receives
 * (returns 0 for even game with 7.5 komi). */
/* This is just an approximation since in reality, handicap seems to be usually
 * non-linear. */
float board_effective_handicap(struct board *b);

/* Decide if the given player wins counting on the board, considering
 * that given groups are dead. (To get the list of dead groups, use
 * e.g. groups_of_status().) */
bool pass_is_safe(struct board *b, enum stone color, struct move_queue *mq);


bool is_bad_selfatari_slow(struct board *b, enum stone color, coord_t to);
static inline bool
is_bad_selfatari(struct board *b, enum stone color, coord_t to)
{
	/* More than one immediate liberty, thumbs up! */
	if (immediate_liberty_count(b, to) > 1)
		return false;

	return is_bad_selfatari_slow(b, color, to);
}

bool is_border_ladder(struct board *b, coord_t coord, enum stone lcolor);
bool is_middle_ladder(struct board *b, coord_t coord, enum stone lcolor);
static inline bool
is_ladder(struct board *b, coord_t coord, group_t laddered,
          bool border_ladders, bool middle_ladders)
{
	enum stone lcolor = board_at(b, group_base(laddered));

	if (DEBUGL(6))
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord, b), stone2str(lcolor), coord2sstr(laddered, b));

	/* First, special-case first-line "ladders". This is a huge chunk
	 * of ladders we actually meet and want to play. */
	if (border_ladders
	    && neighbor_count_at(b, coord, S_OFFBOARD) == 1
	    && neighbor_count_at(b, coord, lcolor) == 1) {
		bool l = is_border_ladder(b, coord, lcolor);
		if (DEBUGL(6)) fprintf(stderr, "border ladder solution: %d\n", l);
		return l;
	}

	if (middle_ladders) {
		bool l = is_middle_ladder(b, coord, lcolor);
		if (DEBUGL(6)) fprintf(stderr, "middle ladder solution: %d\n", l);
		return l;
	}

	if (DEBUGL(6)) fprintf(stderr, "no ladder to be checked\n");
	return false;
}


static inline int
coord_edge_distance(coord_t c, struct board *b)
{
	int x = coord_x(c, b), y = coord_y(c, b);
	int dx = x > board_size(b) / 2 ? board_size(b) - 1 - x : x;
	int dy = y > board_size(b) / 2 ? board_size(b) - 1 - y : y;
	return (dx < dy ? dx : dy) - 1 /* S_OFFBOARD */;
}

static inline int
coord_gridcular_distance(coord_t c1, coord_t c2, struct board *b)
{
	int dx = abs(coord_dx(c1, c2, b)), dy = abs(coord_dy(c1, c2, b));
	return dx + dy + (dx > dy ? dx : dy);
}

#endif
