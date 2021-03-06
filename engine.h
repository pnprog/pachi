#ifndef PACHI_ENGINE_H
#define PACHI_ENGINE_H

#include "gtp.h"
#include "ownermap.h"

enum engine_id {
	E_RANDOM,
	E_REPLAY,
	E_MONTECARLO,	
	E_PATTERNSCAN,
	E_PATTERNPLAY,
	E_JOSEKISCAN,
	E_JOSEKIPLAY,
	E_UCT,
#ifdef DISTRIBUTED
	E_DISTRIBUTED,
#endif
#ifdef DCNN
	E_DCNN,
#endif
	E_MAX,
};

typedef struct engine engine_t;

typedef void (*engine_init_t)(engine_t *e, char *arg, board_t *b);
typedef enum parse_code (*engine_notify_t)(engine_t *e, board_t *b, int id, char *cmd, char *args, char **reply);
typedef void (*engine_board_print_t)(engine_t *e, board_t *b, FILE *f);
typedef char *(*engine_notify_play_t)(engine_t *e, board_t *b, move_t *m, char *enginearg);
typedef char *(*engine_result_t)(engine_t *e, board_t *b);
typedef char *(*engine_chat_t)(engine_t *e, board_t *b, bool in_game, char *from, char *cmd);
typedef coord_t (*engine_genmove_t)(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive);
typedef void  (*engine_best_moves_t)(engine_t *e, board_t *b, time_info_t *ti, enum stone color, 
				     coord_t *best_c, float *best_r, int nbest);
typedef char *(*engine_genmoves_t)(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
				 char *args, bool pass_all_alive, void **stats_buf, int *stats_size);
typedef void (*engine_evaluate_t)(engine_t *e, board_t *b, time_info_t *ti, floating_t *vals, enum stone color);
typedef void (*engine_analyze_t)(engine_t *e, board_t *b, enum stone color, int start);
typedef void (*engine_dead_group_list_t)(engine_t *e, board_t *b, move_queue_t *mq);
typedef void (*engine_stop_t)(engine_t *e);
typedef void (*engine_done_t)(engine_t *e);
typedef ownermap_t* (*engine_ownermap_t)(engine_t *e, board_t *b);
typedef void (*engine_livegfx_hook_t)(engine_t *e);

/* This is engine data structure. A new engine instance is spawned
 * for each new game during the program lifetime. */
struct engine {
	int   id;
	char *name;
	char *comment;

	/* If set, do not reset the engine state on clear_board. */
	bool keep_on_clear;

	engine_notify_t          notify;
	engine_board_print_t     board_print;
	engine_notify_play_t     notify_play;
	engine_chat_t            chat;
	engine_result_t          result;

	/* Generate a move. If pass_all_alive is true, <pass> shall be generated only
	 * if all stones on the board can be considered alive, without regard to "dead"
	 * considered stones. */
	engine_genmove_t         genmove;
	engine_genmove_t         genmove_analyze;

	/* Used by distributed engine */
	engine_genmoves_t        genmoves;

	/* List best moves for current position.
	 * Call engine_best_move() for data to be initialized correctly. */
	engine_best_moves_t      best_moves;

	/* Evaluate feasibility of player @color playing at all free moves. Will
	 * simulate each move from b->f[i] for time @ti, then set
	 * 1-max(opponent_win_likelihood) in vals[i]. */
	engine_evaluate_t        evaluate;

	/* Tell engine to start pondering for the sake of frontend running Pachi. */
	engine_analyze_t         analyze;
	
	/* One dead group per queued move (coord_t is (ab)used as group_t). */
	engine_dead_group_list_t dead_group_list;

	/* Pause any background thinking being done, but do not tear down
	 * any data structures yet. */
	engine_stop_t            stop;

	/* e->data and e will be free()d by caller afterwards. */
	engine_done_t            done;

	/* Return current ownermap, if engine supports it. */
	engine_ownermap_t        ownermap;
	
	/* GoGui hook */
	engine_livegfx_hook_t   livegfx_hook;
	
	void *data;
};


/* Initialize engine. Call engine_done() later when finished with it. */
void engine_init(engine_t *e, int id, char *e_arg, board_t *b);

/* Clean up what engine_init() did. */
void engine_done(engine_t *e);

/* Allocate and initialize a new engine.
 * You are responsible for calling engine_done() and free() on it when done. */
engine_t* new_engine(int id, char *e_arg, board_t *b);

/* engine_done() + engine_init(), more or less. */
void engine_reset(engine_t *e, board_t *b, char *e_arg);


/* Convenience functions for engine actions: */
void engine_board_print(engine_t *e, board_t *b, FILE *f);
void engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color,
		       coord_t *best_c, float *best_r, int nbest);
struct ownermap* engine_ownermap(engine_t *e, board_t *b);


/* Engines best moves common code */

/* For engines best_move(): Add move @c with prob @r to best moves @best_c, @best_r */
void best_moves_add(coord_t c, float r, coord_t *best_c, float *best_r, int nbest);
void best_moves_add_full(coord_t c, float r, void *d, coord_t *best_c, float *best_r, void **best_d, int nbest);
int  best_moves_print(board_t *b, char *str, coord_t *best_c, int nbest);


#endif
