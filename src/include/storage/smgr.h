/*-------------------------------------------------------------------------
 *
 * smgr.h
 *	  storage manager switch public interface declarations.
 *
 *
 * Portions Copyright (c) 2006-2008, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/smgr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SMGR_H
#define SMGR_H

#include "fmgr.h"
#include "lib/ilist.h"
#include "storage/block.h"
#include "storage/relfilenode.h"
#include "storage/dbdirnode.h"
#include "fd.h"

struct f_smgr;

/*
 * smgr.c maintains a table of SMgrRelation objects, which are essentially
 * cached file handles.  An SMgrRelation is created (if not already present)
 * by smgropen(), and destroyed by smgrclose().  Note that neither of these
 * operations imply I/O, they just create or destroy a hashtable entry.
 * (But smgrclose() may release associated resources, such as OS-level file
 * descriptors.)
 *
 * An SMgrRelation may have an "owner", which is just a pointer to it from
 * somewhere else; smgr.c will clear this pointer if the SMgrRelation is
 * closed.  We use this to avoid dangling pointers from relcache to smgr
 * without having to make the smgr explicitly aware of relcache.  There
 * can't be more than one "owner" pointer per SMgrRelation, but that's
 * all we need.
 *
 * SMgrRelations that do not have an "owner" are considered to be transient,
 * and are deleted at end of transaction.
 */
typedef struct SMgrRelationData
{
	/* rnode is the hashtable lookup key, so it must be first! */
	RelFileNodeBackend smgr_rnode;		/* relation physical identifier */

	/* pointer to owning pointer, or NULL if none */
	struct SMgrRelationData **smgr_owner;

	/*
	 * These next three fields are not actually used or manipulated by smgr,
	 * except that they are reset to InvalidBlockNumber upon a cache flush
	 * event (in particular, upon truncation of the relation).  Higher levels
	 * store cached state here so that it will be reset when truncation
	 * happens.  In all three cases, InvalidBlockNumber means "unknown".
	 */
	BlockNumber smgr_targblock; /* current insertion target block */
	BlockNumber smgr_fsm_nblocks;		/* last known size of fsm fork */
	BlockNumber smgr_vm_nblocks;	/* last known size of vm fork */

	/* additional public fields may someday exist here */

	/*
	 * Fields below here are intended to be private to smgr.c and its
	 * submodules.  Do not touch them from elsewhere.
	 */
	/* Obsolete storage manager selector, should not be used for any particular purpose */
	int			smgr_which;
	const struct f_smgr *smgr; /* storage manager selector */
	const struct f_smgr_ao *smgr_ao; /* storage manager selector */

	/* for md.c; NULL for forks that are not open */
	struct _MdfdVec *md_fd[MAX_FORKNUM + 1];

	/* if unowned, list link in list of all unowned SMgrRelations */
	dlist_node	node;
	
} SMgrRelationData;

typedef SMgrRelationData *SMgrRelation;

#define SmgrIsTemp(smgr) \
	RelFileNodeBackendIsTemp((smgr)->smgr_rnode)


/*
 * This struct of function pointers defines the API between smgr.c and
 * any individual storage manager module.  Note that smgr subfunctions are
 * generally expected to report problems via elog(ERROR).  An exception is
 * that smgr_unlink should use elog(WARNING), rather than erroring out,
 * because we normally unlink relations during post-commit/abort cleanup,
 * and so it's too late to raise an error.  Also, various conditions that
 * would normally be errors should be allowed during bootstrap and/or WAL
 * recovery --- see comments in md.c for details.
 */
typedef struct f_smgr
{
	void		(*smgr_init) (void);	/* may be NULL */
	void		(*smgr_shutdown) (void);	/* may be NULL */
	void		(*smgr_close) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_create) (SMgrRelation reln, ForkNumber forknum,
								bool isRedo);

	void		(*smgr_create_ao) (RelFileNodeBackend rnode, int32 segmentFileNum, bool isRedo);

	bool		(*smgr_exists) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_unlink) (RelFileNodeBackend rnode, ForkNumber forknum,
								bool isRedo, char relstorage);
	void		(*smgr_extend) (SMgrRelation reln, ForkNumber forknum,
								BlockNumber blocknum, char *buffer, bool skipFsync);
	void		(*smgr_prefetch) (SMgrRelation reln, ForkNumber forknum,
								  BlockNumber blocknum);
	void		(*smgr_read) (SMgrRelation reln, ForkNumber forknum,
							  BlockNumber blocknum, char *buffer);
	void		(*smgr_write) (SMgrRelation reln, ForkNumber forknum,
							   BlockNumber blocknum, char *buffer, bool skipFsync);
	void		(*smgr_writeback) (SMgrRelation reln, ForkNumber forknum,
								   BlockNumber blocknum, BlockNumber nblocks);
	BlockNumber (*smgr_nblocks) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_truncate) (SMgrRelation reln, ForkNumber forknum,
								  BlockNumber nblocks);
	void		(*smgr_immedsync) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_pre_ckpt) (void);		/* may be NULL */
	void		(*smgr_sync) (void);	/* may be NULL */
	void		(*smgr_post_ckpt) (void);		/* may be NULL */
} f_smgr;

typedef int SMGRFile;


typedef struct f_smgr_ao {
	int64       (*smgr_NonVirtualCurSeek) (SMGRFile file);
	int64 		(*smgr_FileSeek) (SMGRFile file, int64 offset, int whence);
	void 		(*smgr_FileClose)(SMGRFile file);
	int         (*smgr_FileTruncate) (SMGRFile file, int64 offset);
	SMGRFile    (*smgr_AORelOpenSegFile) (
		Oid reloid,
		char * nspname, 
		char * relname,
		FileName fileName,
		int fileFlags,
		int fileMode,
		int64 modcount);
	int         (*smgr_FileWrite)(SMGRFile file, char *buffer, int amount);
    int         (*smgr_FileRead)(SMGRFile file, char *buffer, int amount);
	int	        (*smgr_FileSync)(SMGRFile file);
} f_smgr_ao;


typedef void (*smgr_init_hook_type) (void);
typedef void (*smgrao_init_hook_type) (void);
typedef void (*smgr_shutdown_hook_type) (void);
typedef void (*smgrao_shutdown_hook_type) (void);

extern PGDLLIMPORT smgrao_init_hook_type smgrao_init_hook;
extern PGDLLIMPORT smgr_init_hook_type smgr_init_hook;
extern PGDLLIMPORT smgr_shutdown_hook_type smgr_shutdown_hook;
extern void smgr_init_standard(void);
extern void smgr_shutdown_standard(void);


typedef const f_smgr *(*smgr_hook_type) (BackendId backend, RelFileNode rnode);
typedef const f_smgr_ao *(*smgrao_hook_type)();
extern PGDLLIMPORT smgr_hook_type smgr_hook;
extern PGDLLIMPORT smgrao_hook_type smgrao_hook;
extern const f_smgr *smgr_standard(BackendId backend, RelFileNode rnode);

extern const f_smgr *smgr(BackendId backend, RelFileNode rnode);
extern const f_smgr_ao *smgrao(void);

extern void smgrinit(void);
extern SMgrRelation smgropen(RelFileNode rnode, BackendId backend);
extern bool smgrexists(SMgrRelation reln, ForkNumber forknum);
extern void smgrsetowner(SMgrRelation *owner, SMgrRelation reln);
extern void smgrclearowner(SMgrRelation *owner, SMgrRelation reln);
extern void smgrclose(SMgrRelation reln);
extern void smgrcloseall(void);
extern void smgrclosenode(RelFileNodeBackend rnode);
extern void smgrcreate(SMgrRelation reln, ForkNumber forknum, bool isRedo);
extern void smgrcreate_ao(SMgrRelation reln, int32 segmentFileNum, bool isRedo);
extern void smgrdounlink(SMgrRelation reln, bool isRedo, char relstorage);
extern void smgrdounlinkall(SMgrRelation *rels, int nrels, bool isRedo, char *relstorages);
extern void smgrextend(SMgrRelation reln, ForkNumber forknum,
		   BlockNumber blocknum, char *buffer, bool skipFsync);
extern void smgrprefetch(SMgrRelation reln, ForkNumber forknum,
			 BlockNumber blocknum);
extern void smgrread(SMgrRelation reln, ForkNumber forknum,
		 BlockNumber blocknum, char *buffer);
extern void smgrwrite(SMgrRelation reln, ForkNumber forknum,
		  BlockNumber blocknum, char *buffer, bool skipFsync);
extern BlockNumber smgrnblocks(SMgrRelation reln, ForkNumber forknum);
extern void smgrtruncate(SMgrRelation reln, ForkNumber forknum,
			 BlockNumber nblocks);
extern void smgrimmedsync(SMgrRelation reln, ForkNumber forknum);
extern void smgrpreckpt(void);
extern void smgrsync(void);
extern void smgrpostckpt(void);
extern void AtEOXact_SMgr(void);

/* smgrtype.c */
extern Datum smgrout(PG_FUNCTION_ARGS);
extern Datum smgrin(PG_FUNCTION_ARGS);
extern Datum smgreq(PG_FUNCTION_ARGS);
extern Datum smgrne(PG_FUNCTION_ARGS);

/*
 * Hook for plugins to collect statistics from storage functions
 * For example, disk quota extension will use these hooks to
 * detect active tables.
 */
typedef void (*file_create_hook_type)(RelFileNodeBackend rnode);
extern PGDLLIMPORT file_create_hook_type file_create_hook;

typedef void (*file_extend_hook_type)(RelFileNodeBackend rnode);
extern PGDLLIMPORT file_extend_hook_type file_extend_hook;

typedef void (*file_truncate_hook_type)(RelFileNodeBackend rnode);
extern PGDLLIMPORT file_truncate_hook_type file_truncate_hook;

typedef void (*file_unlink_hook_type)(RelFileNodeBackend rnode);
extern PGDLLIMPORT file_unlink_hook_type file_unlink_hook;

/* Test utility */
extern void GetMdCxtStat(uint64 *nBlocks, uint64 *nChunks, uint64 *currentAvailable,
			uint64 *allAllocated, uint64 *allFreed, uint64 *maxHeld);

#endif   /* SMGR_H */
