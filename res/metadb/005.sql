-- Add new datasource table


CREATE TABLE datasource (
       id INTEGER PRIMARY KEY,
       name TEXT
);
CREATE INDEX datasource_name_idx ON datasource(name);

INSERT INTO datasource (id, name) VALUES (1, "file");


-- Recreate artist table
DROP TABLE artist;
CREATE TABLE artist (
       id INTEGER PRIMARY KEY,
       title TEXT,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE CASCADE,
       ext_id TEXT,
       UNIQUE (title, ds_id, ext_id)
);

CREATE INDEX artist_title_idx ON artist(title);
CREATE INDEX artist_ext_id_idx ON artist(ext_id);


-- Recreate album table
DROP TABLE album;
CREATE TABLE album (
       id INTEGER PRIMARY KEY,
       title TEXT,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE CASCADE,
       artist_id INTEGER REFERENCES artist(id) ON DELETE CASCADE,
       ext_id TEXT,
       UNIQUE (title, ds_id, ext_id)
);

CREATE INDEX album_title_idx ON album(title);
CREATE INDEX album_ext_id_idx ON album(ext_id);


-- Recreate audioitem table

DROP TABLE audioitem;
CREATE TABLE audioitem (
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       title TEXT,
       album_id INTEGER REFERENCES album(id) ON DELETE CASCADE,       
       artist_id INTEGER REFERENCES artist(id) ON DELETE CASCADE,
       duration INTEGER,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE CASCADE,
       UNIQUE (item_id, ds_id)
       );

CREATE INDEX audioitem_item_id_idx ON audioitem(item_id);

-- Create album art table
CREATE TABLE albumart (
       album_id INTEGER REFERENCES album(id) ON DELETE CASCADE,
       url TEXT,
       width INTEGER,
       height INTEGER,
       UNIQUE (album_id, url)
);
CREATE INDEX albumart_album_id_idx ON albumart(album_id);
CREATE INDEX albumart_url_idx ON albumart(url);

-- Create artist picture table
CREATE TABLE artistpic (
       artist_id INTEGER REFERENCES artist(id) ON DELETE CASCADE,
       url TEXT,
       width INTEGER,
       height INTEGER,
       UNIQUE (artist_id, url)
);
CREATE INDEX artistpic_artid_id_idx ON artistpic(artist_id);
CREATE INDEX artistpic_url_idx ON artistpic(url);
