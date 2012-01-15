-- Migrate the videoitem.restartposition to item

ALTER TABLE item ADD COLUMN restartposition INTEGER DEFAULT 0;

UPDATE item SET restartposition = (SELECT restartposition FROM videoitem WHERE item_id = id);

CREATE TABLE videoitem_tmp(
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       title TEXT,
       duration INTEGER,
       format TEXT);

INSERT INTO videoitem_tmp SELECT item_id, title, duration, format FROM videoitem;
DROP TABLE videoitem;

ALTER TABLE videoitem_tmp RENAME TO videoitem;

CREATE INDEX videoitem_item_id_idx ON videoitem(item_id);

