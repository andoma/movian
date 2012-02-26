
-- Recreate videoitem

DROP TABLE videoitem;

CREATE TABLE "videoitem"(
       id INTEGER PRIMARY KEY,
       item_id INTEGER REFERENCES item(id) ON DELETE CASCADE,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE CASCADE,
       ext_id TEXT,
       title TEXT,
       duration INTEGER,
       type INTEGER NOT NULL,
       format TEXT,
       tagline TEXT,
       description TEXT,
       year INTEGER,
       rating INTEGER,
       rate_count INTEGER,
       imdb_id TEXT,
       UNIQUE(item_id, ds_id, ext_id)
);

CREATE INDEX videoitem_item_id_idx ON videoitem(item_id);
CREATE INDEX videoitem_imdb_id_idx ON videoitem(imdb_id);

--

CREATE TABLE videoart (
       videoitem_id INTEGER REFERENCES videoitem(id) ON DELETE CASCADE,
       type INTEGER,
       url TEXT,
       width INTEGER,
       height INTEGER,
       UNIQUE (videoitem_id, url)
);

CREATE INDEX videoart_videoitem_id_idx ON videoart(videoitem_id);

-- 

DROP TABLE stream;

CREATE TABLE videostream (
       videoitem_id INTEGER REFERENCES videoitem(id) ON DELETE CASCADE,
       streamindex INTEGER,
       info TEXT,
       isolang TEXT,
       codec TEXT,
       mediatype TEXT,
       disposition INTEGER,
       title TEXT
       );

CREATE INDEX videostream_item_id_idx ON videostream(videoitem_id);
