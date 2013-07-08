drop table if exists blocks, deleted_files, files, paths, attributes,
                     configuration;

create table files (inode bigint not null auto_increment,
                    uid int unsigned not null,
                    gid int unsigned not null,
                    mode int unsigned not null default 511,
                    atime timestamp not null default current_timestamp,
                    mtime timestamp not null default current_timestamp,
                    ctime timestamp not null default current_timestamp
                                    on update current_timestamp,
                    flags int unsigned not null default 0,
                    size bigint unsigned not null default 0,
                    
                    primary key (inode));

-- Linux filesystems support extended file attributes as key value pairs.
create table attributes (inode bigint,
                         name varchar(255),
                         value text,

                         foreign key (inode) references files (inode)
                                     on delete cascade,
                                     
                         primary key (inode, name));

-- Each row in the paths table is a directory table
-- XXX JLG: I think we want utf8_general_cs                         
create table paths (directory bigint,
                    filename varchar(255)
                             character set 'utf8' collate 'binary'
                             not null,
                    inode bigint not null,
                    
                    foreign key (directory) references files (inode),
                    foreign key (inode) references files (inode),
                    
                    key (inode, directory), /* reverse lookup */
                    primary key (directory, filename));
                   
create table symlinks (inode bigint not null,
                       target text
                       character set 'utf8' collate 'binary' not null,
                       
                       foreign key (inode) references files (inode)
                                 on delete cascade,
                       primary key (inode));
                       
                    
-- Each regular file has zero or more blocks associated with it.  The
-- size of each file is recorded in the files table.  Missing entries
-- in the blocks table are assumed to be zero.  All blocks are
-- full-size.  If a block is less than the full size, the missing
-- portion can be assumed to be zero.
--
-- XXX JLG - we only distribute on inode.  For large files it would be
-- much better to distribute them across multiple disks.  The best
-- solution might be to have two block stores, one for small files and
-- one for large ones.
create table blocks (inode bigint not null,
                     `offset` bigint unsigned not null,
                     data blob not null,
                     
                     foreign key (inode) references files (inode)
                                 on delete cascade,
                                 
                     primary key (inode, `offset`) /*$ distribute=1 */);

-- Files which have been deleted, but may have sessions referencing
-- them are here.
create table deleted_files (inode bigint not null,

                            foreign key (inode) references files (inode)
                                 on delete cascade,
                            primary key (inode));
                            
-- Filesystem parameters are kept here.  Generally these are set at
-- format time.  At least clients won't be checking them for changes.
create table configuration (parameter varchar(255),
                            value varchar(255),

                            primary key (parameter));

-- The schema version                            
insert into configuration values ('version', 1);
insert into configuration values ('blocksize', 512);

-- Create the root directory
-- 16895 -> 0777 + directory
insert into files (inode, uid, gid, mode) values (1, 1, 1, 16895);
