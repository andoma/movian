CREATE TABLE item (
       stash TEXT,
       k TEXT,
       lastaccess INTEGER,
       expiry INTEGER,
       modtime INTEGER,
       etag TEXT,
       payload BLOB,
       UNIQUE(k, stash) ON CONFLICT FAIL);

CREATE INDEX item_key_idx ON item(stash, k);
