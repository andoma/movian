-- schema-upgrade:disable-fk

CREATE TABLE playcount_tmp(url, val);
INSERT INTO playcount_tmp SELECT url, playcount FROM item WHERE playcount > 0;
INSERT OR IGNORE INTO kvstore.url(url) SELECT url FROM playcount_tmp;
INSERT INTO kvstore.url_kv SELECT id, 1, "playcount", val FROM playcount_tmp, kvstore.url WHERE playcount_tmp.url = url.url;
DROP TABLE playcount_tmp;

CREATE TABLE restartposition_tmp(url, val);
INSERT INTO restartposition_tmp SELECT url, restartposition FROM item WHERE restartposition > 0;
INSERT OR IGNORE INTO kvstore.url(url) SELECT url FROM restartposition_tmp;
INSERT INTO kvstore.url_kv SELECT id, 1, "restartposition", val FROM restartposition_tmp, kvstore.url WHERE restartposition_tmp.url = url.url;
DROP TABLE restartposition_tmp;

CREATE TABLE lastplay_tmp(url, val);
INSERT INTO lastplay_tmp SELECT url, lastplay FROM item WHERE lastplay > 0;
INSERT OR IGNORE INTO kvstore.url(url) SELECT url FROM lastplay_tmp;
INSERT INTO kvstore.url_kv SELECT id, 1, "lastplay", val FROM lastplay_tmp, kvstore.url WHERE lastplay_tmp.url = url.url;
DROP TABLE lastplay_tmp;


CREATE TABLE item_tmp (
       id INTEGER PRIMARY KEY,
       url TEXT NOT NULL UNIQUE,
       contenttype INTEGER,
       mtime INTEGER,
       parent INTEGER,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE SET NULL,
       usertitle TEXT,
       indexstatus DEFAULT 0);


INSERT INTO item_tmp SELECT id,url,contenttype,mtime,parent,ds_id,usertitle,indexstatus FROM item;

DROP TABLE item;

ALTER TABLE item_tmp RENAME TO item;

CREATE INDEX item_url_idx ON item(url);
