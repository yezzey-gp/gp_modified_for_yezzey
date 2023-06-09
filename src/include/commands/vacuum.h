/*-------------------------------------------------------------------------
 *
 * vacuum.h
 *	  header file for postgres vacuum cleaner and statistics analyzer
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/vacuum.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef VACUUM_H
#define VACUUM_H

#include "access/htup.h"
#include "catalog/pg_statistic.h"
#include "catalog/pg_type.h"
#include "nodes/parsenodes.h"
#include "storage/buf.h"
#include "storage/lock.h"
#include "utils/relcache.h"
#include "utils/tqual.h"

/*----------
 * ANALYZE builds one of these structs for each attribute (column) that is
 * to be analyzed.  The struct and subsidiary data are in anl_context,
 * so they live until the end of the ANALYZE operation.
 *
 * The type-specific typanalyze function is passed a pointer to this struct
 * and must return TRUE to continue analysis, FALSE to skip analysis of this
 * column.  In the TRUE case it must set the compute_stats and minrows fields,
 * and can optionally set extra_data to pass additional info to compute_stats.
 * minrows is its request for the minimum number of sample rows to be gathered
 * (but note this request might not be honored, eg if there are fewer rows
 * than that in the table).
 *
 * The compute_stats routine will be called after sample rows have been
 * gathered.  Aside from this struct, it is passed:
 *		fetchfunc: a function for accessing the column values from the
 *				   sample rows
 *		samplerows: the number of sample tuples
 *		totalrows: estimated total number of rows in relation
 * The fetchfunc may be called with rownum running from 0 to samplerows-1.
 * It returns a Datum and an isNull flag.
 *
 * compute_stats should set stats_valid TRUE if it is able to compute
 * any useful statistics.  If it does, the remainder of the struct holds
 * the information to be stored in a pg_statistic row for the column.  Be
 * careful to allocate any pointed-to data in anl_context, which will NOT
 * be CurrentMemoryContext when compute_stats is called.
 *
 * Note: for the moment, all comparisons done for statistical purposes
 * should use the database's default collation (DEFAULT_COLLATION_OID).
 * This might change in some future release.
 *----------
 */
typedef struct VacAttrStats *VacAttrStatsP;

typedef Datum (*AnalyzeAttrFetchFunc) (VacAttrStatsP stats, int rownum,
												   bool *isNull);

typedef void (*AnalyzeAttrComputeStatsFunc) (VacAttrStatsP stats,
											  AnalyzeAttrFetchFunc fetchfunc,
														 int samplerows,
														 double totalrows);

typedef struct VacAttrStats
{
	/*
	 * These fields are set up by the main ANALYZE code before invoking the
	 * type-specific typanalyze function.
	 *
	 * Note: do not assume that the data being analyzed has the same datatype
	 * shown in attr, ie do not trust attr->atttypid, attlen, etc.  This is
	 * because some index opclasses store a different type than the underlying
	 * column/expression.  Instead use attrtypid, attrtypmod, and attrtype for
	 * information about the datatype being fed to the typanalyze function.
	 */
	Form_pg_attribute attr;		/* copy of pg_attribute row for column */
	Oid			attrtypid;		/* type of data being analyzed */
	int32		attrtypmod;		/* typmod of data being analyzed */
	Form_pg_type attrtype;		/* copy of pg_type row for attrtypid */
	char		relstorage;		/* pg_class.relstorage for table */
	MemoryContext anl_context;	/* where to save long-lived data */
	int16		elevel;			/* set to LOG for ANALYZE VERBOSE */

	/*
	 * These fields must be filled in by the typanalyze routine, unless it
	 * returns FALSE.
	 */
	AnalyzeAttrComputeStatsFunc compute_stats;	/* function pointer */
	int			minrows;		/* Minimum # of rows wanted for stats */
	void	   *extra_data;		/* for extra type-specific data */

	/*
	 * These fields are to be filled in by the compute_stats routine. (They
	 * are initialized to zero when the struct is created.)
	 */
	bool		stats_valid;
	float4		stanullfrac;	/* fraction of entries that are NULL */
	int32		stawidth;		/* average width of column values */
	float4		stadistinct;	/* # distinct values */
	int16		stakind[STATISTIC_NUM_SLOTS];
	Oid			staop[STATISTIC_NUM_SLOTS];
	int			numnumbers[STATISTIC_NUM_SLOTS];
	float4	   *stanumbers[STATISTIC_NUM_SLOTS];
	int			numvalues[STATISTIC_NUM_SLOTS];
	Datum	   *stavalues[STATISTIC_NUM_SLOTS];

	bytea *stahll;			/* storing hyperloglog counter for sampled data */
	bytea *stahll_full;			/* storing hyperloglog counter for entire table scan */
	/*
	 * These fields describe the stavalues[n] element types. They will be
	 * initialized to match attrtypid, but a custom typanalyze function might
	 * want to store an array of something other than the analyzed column's
	 * elements. It should then overwrite these fields.
	 */
	Oid			statypid[STATISTIC_NUM_SLOTS];
	int16		statyplen[STATISTIC_NUM_SLOTS];
	bool		statypbyval[STATISTIC_NUM_SLOTS];
	char		statypalign[STATISTIC_NUM_SLOTS];

	/*
	 * These fields are private to the main ANALYZE code and should not be
	 * looked at by type-specific functions.
	 */
	int			tupattnum;		/* attribute number within tuples */
	HeapTuple  *rows;			/* access info for std fetch function */
	TupleDesc	tupDesc;
	Datum	   *exprvals;		/* access info for index fetch function */
	bool	   *exprnulls;
	int			rowstride;
	bool		merge_stats;
} VacAttrStats;

/*
 * To avoid consuming too much memory during analysis and/or too much space
 * in the resulting pg_statistic rows, ANALYZE ignores varlena datums that are wider
 * than WIDTH_THRESHOLD (after detoasting!).  This is legitimate for MCV
 * and distinct-value calculations since a wide value is unlikely to be
 * duplicated at all, much less be a most-common value.  For the same reason,
 * ignoring wide values will not affect our estimates of histogram bin
 * boundaries very much.
 *
 * NOTE: In upstream, this is private to analyze.c, but GPDB needs it in
 * analyzefuncs.c
 */
#define WIDTH_THRESHOLD  1024

/*
 * VPgClassStats is used to hold the stats information that are stored in
 * pg_class. It is sent from QE to QD in a special libpq message , when a
 * QE runs VACUUM on a table.
 */
typedef struct VPgClassStats
{
	Oid			relid;
	BlockNumber rel_pages;
	double		rel_tuples;
	BlockNumber relallvisible;
} VPgClassStats;

/* GUC parameters */
extern PGDLLIMPORT int default_statistics_target;		/* PGDLLIMPORT for
														 * PostGIS */
extern int	vacuum_freeze_min_age;
extern int	vacuum_freeze_table_age;
extern int	vacuum_multixact_freeze_min_age;
extern int	vacuum_multixact_freeze_table_age;


/* in commands/vacuum.c */
extern void vacuum(VacuumStmt *vacstmt, Oid relid, bool do_toast,
	   BufferAccessStrategy bstrategy, bool for_wraparound, bool isTopLevel);
extern void vac_open_indexes(Relation relation, LOCKMODE lockmode,
				 int *nindexes, Relation **Irel);
extern void vac_close_indexes(int nindexes, Relation *Irel, LOCKMODE lockmode);
extern double vac_estimate_reltuples(Relation relation, bool is_analyze,
					   BlockNumber total_pages,
					   BlockNumber scanned_pages,
					   double scanned_tuples);
extern void vac_send_relstats_to_qd(Relation relation,
						BlockNumber num_pages,
						double num_tuples,
						BlockNumber num_all_visible_pages);
extern void vac_update_relstats(Relation relation,
					BlockNumber num_pages,
					double num_tuples,
					BlockNumber num_all_visible_pages,
					bool hasindex,
					TransactionId frozenxid,
					MultiXactId minmulti,
					bool in_outer_xact,
					bool isvacuum);
extern void vacuum_set_xid_limits(Relation rel,
					  int freeze_min_age, int freeze_table_age,
					  int multixact_freeze_min_age,
					  int multixact_freeze_table_age,
					  TransactionId *oldestXmin,
					  TransactionId *freezeLimit,
					  TransactionId *xidFullScanLimit,
					  MultiXactId *multiXactCutoff,
					  MultiXactId *mxactFullScanLimit);
extern void vac_update_datfrozenxid(void);
extern void vacuum_delay_point(void);

extern bool vacuumStatement_IsTemporary(Relation onerel);

/* in commands/vacuumlazy.c */
extern void lazy_vacuum_rel(Relation onerel, VacuumStmt *vacstmt,
				BufferAccessStrategy bstrategy);
extern void vacuum_appendonly_rel(Relation aorel, VacuumStmt *vacstmt);
extern void vacuum_appendonly_fill_stats(Relation aorel, Snapshot snapshot,
										 BlockNumber *rel_pages, double *rel_tuples,
										 bool *relhasindex);
extern int vacuum_appendonly_indexes(Relation aoRelation, VacuumStmt *vacstmt, Bitmapset *dead_segs);
extern void vacuum_aocs_rel(Relation aorel, void *vacrelstats, bool isVacFull);

/* in commands/analyze.c */
extern void analyze_rel(Oid relid, VacuumStmt *vacstmt,
			bool in_outer_xact, BufferAccessStrategy bstrategy);

extern void analyzeStatement(VacuumStmt *vacstmt, List *relids, BufferAccessStrategy start, bool isTopLevel);
extern bool std_typanalyze(VacAttrStats *stats);
extern double anl_random_fract(void);
extern double anl_init_selection_state(int n);
extern double anl_get_next_S(double t, int n, double *stateptr);

extern int acquire_sample_rows(Relation onerel, int elevel,
							   HeapTuple *rows, int targrows,
							   double *totalrows, double *totaldeadrows);
extern int acquire_inherited_sample_rows(Relation onerel, int elevel,
							  HeapTuple *rows, int targrows,
							  double *totalrows, double *totaldeadrows);

/* in commands/analyzefuncs.c */
extern Datum gp_acquire_sample_rows(PG_FUNCTION_ARGS);
extern Oid gp_acquire_sample_rows_col_type(Oid typid);

#endif   /* VACUUM_H */
