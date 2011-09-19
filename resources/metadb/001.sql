CREATE TABLE itemtype (
       id INTEGER PRIMARY KEY,
       name TEXT NOT NULL
       );

INSERT INTO itemtype values (0, 'unknown');
INSERT INTO itemtype values (1, 'dir');
INSERT INTO itemtype values (2, 'file');
INSERT INTO itemtype values (3, 'archive');
INSERT INTO itemtype values (4, 'audio');
INSERT INTO itemtype values (5, 'video');
INSERT INTO itemtype values (6, 'playlist');
INSERT INTO itemtype values (7, 'dvd');
INSERT INTO itemtype values (8, 'image');
INSERT INTO itemtype values (9, 'album');

CREATE TABLE item (
       id INTEGER PRIMARY KEY,
       url TEXT NOT NULL UNIQUE,
       contenttype INTEGER REFERENCES itemtype(id) ON DELETE RESTRICT,
       mtime INTEGER,
       playcount INTEGER DEFAULT 0,
       lastplay INTEGER,
       parent INTEGER,
       metadataversion INTEGER
       );

CREATE INDEX item_url_idx ON item(url);


CREATE TABLE artist (
       id INTEGER PRIMARY KEY,
       title TEXT
);
CREATE INDEX artist_title_idx ON artist(title);


CREATE TABLE album (
       id INTEGER PRIMARY KEY,
       title TEXT
);
CREATE INDEX album_title_idx ON album(title);


CREATE TABLE audioitem (
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       title TEXT,
       album_id INTEGER REFERENCES album(id) ON DELETE CASCADE,       
       artist_id INTEGER REFERENCES artist(id) ON DELETE CASCADE,
       duration INTEGER
       );
CREATE INDEX audioitem_item_id_idx ON audioitem(item_id);


CREATE TABLE videoitem (
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       title TEXT,
       duration FLOAT,
       restartposition INTEGER DEFAULT 0,
       format TEXT
);
CREATE INDEX videoitem_item_id_idx ON videoitem(item_id);


CREATE TABLE stream (
       item_id INTEGER REFERENCES item(id) ON DELETE CASCADE,
       streamindex INTEGER,
       info TEXT,
       isolang TEXT,
       codec TEXT,
       mediatype TEXT,
       disposition INTEGER
       );

CREATE INDEX stream_item_id_idx ON stream(item_id);
