ALTER TABLE datasource ADD COLUMN prio;
ALTER TABLE datasource ADD COLUMN type INTEGER;
ALTER TABLE datasource ADD COLUMN enabled INTEGER;
ALTER TABLE item ADD COLUMN ds_id INTEGER REFERENCES datasource(id) ON DELETE SET NULL;

UPDATE datasource SET prio=1000001 WHERE name='file';

-- status 3 = complete
ALTER TABLE videoitem ADD COLUMN status DEFAULT 3;
ALTER TABLE videoitem ADD COLUMN preferred;
ALTER TABLE videoitem ADD COLUMN weight;

-- tmdb code rewritten, need to flush

DELETE FROM videoitem WHERE EXISTS (SELECT * FROM datasource WHERE datasource.id = videoitem.ds_id AND datasource.name = 'tmdb');
