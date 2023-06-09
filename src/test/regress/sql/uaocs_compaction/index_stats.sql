-- @Description Tests basic index stats after vacuuming
CREATE TABLE uaocs_index_stats(
          col_int int,
          col_text text,
          col_numeric numeric,
          col_unq int
          ) with(appendonly=true, orientation=column) DISTRIBUTED RANDOMLY;

Create index uaocs_index_stats_int_idx1 on uaocs_index_stats(col_int);
select * from uaocs_index_stats order by col_int;

insert into uaocs_index_stats values(1,'aa',1001,101),(2,'bb',1002,102);

select * from uaocs_index_stats;
update uaocs_index_stats set col_text=' new value' where col_int = 1;
select * from uaocs_index_stats;
vacuum uaocs_index_stats;
SELECT relname, reltuples FROM pg_class WHERE relname = 'uaocs_index_stats';
SELECT relname, reltuples FROM pg_class WHERE relname = 'uaocs_index_stats_int_idx1';

-- A test of index stat for access methods that rely on table tuple count (bitmap, gin)
truncate uaocs_index_stats;
create index uaocs_index_stats_int_idx2 on uaocs_index_stats using bitmap(col_int);

insert into uaocs_index_stats values(1,'aa',1001,101),(2,'bb',1002,102);

SELECT relname, reltuples FROM pg_class WHERE relname = 'uaocs_index_stats_int_idx2';

-- first vacuum collect table stat on segments
vacuum uaocs_index_stats;
-- second vacuum update index stat with table stat
vacuum uaocs_index_stats;

SELECT relname, reltuples FROM pg_class WHERE relname = 'uaocs_index_stats_int_idx2';
