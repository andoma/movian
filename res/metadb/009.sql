CREATE TABLE videocast (
       videoitem_id INTEGER REFERENCES videoitem(id) ON DELETE CASCADE,
       name TEXT,
       character TEXT,
       department TEXT,
       job TEXT,
       "order" INTEGER,
       image TEXT,
       width INTEGER,
       height INTEGER,
       ext_id TEXT,
       UNIQUE (videoitem_id, name, department, job)
);

CREATE INDEX videocast_videoitem_id_idx ON videocast(videoitem_id);
