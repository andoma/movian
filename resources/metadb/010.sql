ALTER TABLE imageitem ADD COLUMN manufacturer TEXT;
ALTER TABLE imageitem ADD COLUMN equipment TEXT;
DELETE FROM imageitem;
