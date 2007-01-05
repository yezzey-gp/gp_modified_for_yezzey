/*
 * findoidjoins.c
 *
 * Copyright (c) 2002-2007, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/tools/findoidjoins/findoidjoins.c,v 1.4 2007/01/05 22:20:04 momjian Exp $
 */
#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"


int
main(int argc, char **argv)
{
	PGconn	   *conn;
	PQExpBufferData sql;
	PGresult   *res;
	PGresult   *pkrel_res;
	PGresult   *fkrel_res;
	char	   *fk_relname;
	char	   *fk_nspname;
	char	   *fk_attname;
	char	   *pk_relname;
	char	   *pk_nspname;
	int			fk,
				pk;				/* loop counters */

	if (argc != 2)
	{
		fprintf(stderr, "Usage:  %s database\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	initPQExpBuffer(&sql);

	appendPQExpBuffer(&sql, "dbname=%s", argv[1]);

	conn = PQconnectdb(sql.data);
	if (PQstatus(conn) == CONNECTION_BAD)
	{
		fprintf(stderr, "connection error:  %s\n", PQerrorMessage(conn));
		exit(EXIT_FAILURE);
	}

	/* Get a list of relations that have OIDs */

	resetPQExpBuffer(&sql);

	appendPQExpBuffer(&sql, "%s",
					  "SET search_path = public;"
					  "SELECT c.relname, (SELECT nspname FROM "
		"pg_catalog.pg_namespace n WHERE n.oid = c.relnamespace) AS nspname "
					  "FROM pg_catalog.pg_class c "
					  "WHERE c.relkind = 'r' "
					  "AND c.relhasoids "
					  "ORDER BY nspname, c.relname"
		);

	res = PQexec(conn, sql.data);
	if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "sql error:  %s\n", PQerrorMessage(conn));
		exit(EXIT_FAILURE);
	}
	pkrel_res = res;

	/* Get a list of columns of OID type (or any OID-alias type) */

	resetPQExpBuffer(&sql);

	appendPQExpBuffer(&sql, "%s",
					  "SELECT c.relname, "
					  "(SELECT nspname FROM pg_catalog.pg_namespace n WHERE n.oid = c.relnamespace) AS nspname, "
					  "a.attname "
					  "FROM pg_catalog.pg_class c, pg_catalog.pg_attribute a "
					  "WHERE a.attnum > 0 AND c.relkind = 'r' "
					  "AND a.attrelid = c.oid "
					  "AND a.atttypid IN ('pg_catalog.oid'::regtype, "
					  " 'pg_catalog.regclass'::regtype, "
					  " 'pg_catalog.regoper'::regtype, "
					  " 'pg_catalog.regoperator'::regtype, "
					  " 'pg_catalog.regproc'::regtype, "
					  " 'pg_catalog.regprocedure'::regtype, "
					  " 'pg_catalog.regtype'::regtype) "
					  "ORDER BY nspname, c.relname, a.attnum"
		);

	res = PQexec(conn, sql.data);
	if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "sql error:  %s\n", PQerrorMessage(conn));
		exit(EXIT_FAILURE);
	}
	fkrel_res = res;

	/*
	 * For each column and each relation-having-OIDs, look to see if the
	 * column contains any values matching entries in the relation.
	 */

	for (fk = 0; fk < PQntuples(fkrel_res); fk++)
	{
		fk_relname = PQgetvalue(fkrel_res, fk, 0);
		fk_nspname = PQgetvalue(fkrel_res, fk, 1);
		fk_attname = PQgetvalue(fkrel_res, fk, 2);

		for (pk = 0; pk < PQntuples(pkrel_res); pk++)
		{
			pk_relname = PQgetvalue(pkrel_res, pk, 0);
			pk_nspname = PQgetvalue(pkrel_res, pk, 1);

			resetPQExpBuffer(&sql);

			appendPQExpBuffer(&sql,
							  "SELECT	1 "
							  "FROM \"%s\".\"%s\" t1, "
							  "\"%s\".\"%s\" t2 "
							  "WHERE t1.\"%s\"::pg_catalog.oid = t2.oid "
							  "LIMIT 1",
				 fk_nspname, fk_relname, pk_nspname, pk_relname, fk_attname);

			res = PQexec(conn, sql.data);
			if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				fprintf(stderr, "sql error:  %s\n", PQerrorMessage(conn));
				exit(EXIT_FAILURE);
			}

			if (PQntuples(res) != 0)
				printf("Join %s.%s.%s => %s.%s.oid\n",
					   fk_nspname, fk_relname, fk_attname,
					   pk_nspname, pk_relname);

			PQclear(res);
		}
	}

	PQclear(pkrel_res);
	PQclear(fkrel_res);
	PQfinish(conn);

	termPQExpBuffer(&sql);

	exit(EXIT_SUCCESS);
}
