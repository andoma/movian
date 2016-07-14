-- XML parser in pure Lua
--
-- Each node in the "DOM" is either a string (for text data) or a table.
-- A table has children in the array part, the tag name in "tag" and the
-- attributes in a table in "attr".
--
-- { tag="html", attr={foo="bar", plugh="xyzzy"}, ...children... }

local sbyte, schar = string.byte, string.char
local sfind, ssub, gsub = string.find, string.sub, string.gsub
local tinsert, tremove = table.insert, table.remove

local EXCLAIM, QUOT, APOS, MINUS, SLASH, LT, EQ, GT, QUESTION, LSQUARE, RSQUARE = sbyte("!\"'-/<=>?[]", 1, 11)

-- TODO: expand numeric entities to UTF-8
local function sub_hex_ent(s) return schar(tonumber(s, 16)) end
local function sub_dec_ent(s) return schar(tonumber(s)) end

local function unescape(s)
	s = gsub(s, "&lt;", "<")
	s = gsub(s, "&gt;", ">")
	s = gsub(s, "&apos;", "'")
	s = gsub(s, "&quot;", '"')
	s = gsub(s, "&#x(%x+);", sub_hex_ent)
	s = gsub(s, "&#(%d+);", sub_dec_ent)
	s = gsub(s, "&amp;", "&")
	return s
end

local function escape(s)
	s = gsub(s, "&", "&amp;")
	s = gsub(s, "<", "&lt;")
	s = gsub(s, ">", "&gt;")
	s = gsub(s, "'", "&apos;")
	s = gsub(s, '"', "&quot;")
	return s
end

local function isname(c)
	-- true if c is one of: - . : _ or 0-9 A-Z a-z
	return c == 45 or c == 46 or c == 58 or c == 95 or
		(c >= 48 and c <= 57) or
		(c >= 65 and c <= 90) or
		(c >= 97 and c <= 122)
end

local function iswhite(c)
	-- true if c is one of: space, \r, \n or \t
	return c == 32 or c == 13 or c == 10 or c == 9
end

local function parse_xml(s, preserve_white)
	local mark, quote, att
	local p, n = 1, #s
	local stack = {{}}

	local function emit_open_tag(s)
		tinsert(stack, {tag=s, attr={}})
	end

	local function emit_att(k,v)
		stack[#stack].attr[k] = unescape(v)
	end

	local function emit_close_tag()
		local item = tremove(stack)
		tinsert(stack[#stack], item)
	end

	local function emit_text(s)
		if #stack > 1 then
			if preserve_white or not sfind(s, "^[ \r\n\t]*$") then
				tinsert(stack[#stack], unescape(s))
			end
		end
	end

	::parse_text::
	do
		mark = p
		while p <= n and sbyte(s,p) ~= LT do p=p+1 end
		if p > mark then emit_text(ssub(s, mark, p-1)) end
		if sbyte(s,p) == LT then p=p+1 goto parse_element end
		return stack[1][1]
	end

	::parse_element::
	do
		if sbyte(s,p) == SLASH then p=p+1 goto parse_closing_element end
		if sbyte(s,p) == EXCLAIM then p=p+1 goto parse_comment end
		if sbyte(s,p) == QUESTION then p=p+1 goto parse_processing_instruction end
		while iswhite(sbyte(s,p)) do p=p+1 end
		if isname(sbyte(s,p)) then goto parse_element_name end
		return nil, "syntax error in element"
	end

	::parse_comment::
	do
		if sbyte(s,p) == LSQUARE then goto parse_cdata end
		if sbyte(s,p) ~= MINUS then return nil, "syntax error in comment" end p=p+1
		if sbyte(s,p) ~= MINUS then return nil, "syntax error in comment" end p=p+1
		mark = p
		while p <= n do
			if sbyte(s,p) == MINUS and sbyte(s,p+1) == MINUS and sbyte(s,p+2) == GT then
				p=p+3
				goto parse_text
			end
			p=p+1
		end
		return nil, "end of data in comment"
	end

	::parse_cdata::
	do
		if ssub(s, p+1, p+6) ~= "CDATA[" then
			return nil, "syntax error in CDATA section"
		end
		p=p+7
		mark = p
		while p <= n do
			if sbyte(s,p) == RSQUARE and sbyte(s,p+1) == RSQUARE and sbyte(s,p+2) == GT then
				if p > mark then emit_text(ssub(s, mark, p-1)) end
				p=p+3
				goto parse_text
			end
			p=p+1
		end
		return nil, "end of data in CDATA section";
	end

	::parse_processing_instruction::
	do
		while p <= n do
			if sbyte(s,p) == QUESTION and sbyte(s,p+1) == GT then
				p=p+2
				goto parse_text
			end
			p=p+1
		end
		return nil, "end of data in processing instruction"
	end

	::parse_closing_element::
	do
		while iswhite(sbyte(s,p)) do p=p+1 end
		mark = p
		while isname(sbyte(s,p)) do p=p+1 end
		while iswhite(sbyte(s,p)) do p=p+1 end
		if sbyte(s,p) ~= GT then return nil, "syntax error in closing element" end
		emit_close_tag()
		p=p+1
		goto parse_text
	end

	::parse_element_name::
	do
		mark = p
		while isname(sbyte(s,p)) do p=p+1 end
		emit_open_tag(ssub(s, mark, p-1))
		if sbyte(s,p) == GT then p=p+1 goto parse_text end
		if sbyte(s,p) == SLASH and sbyte(s,p+1) == GT then
			emit_close_tag()
			p=p+2
			goto parse_text
		end
		if iswhite(sbyte(s,p)) then goto parse_attributes end
		return nil, "syntax error after element name"
	end

	::parse_attributes::
	do
		while iswhite(sbyte(s,p)) do p=p+1 end
		if isname(sbyte(s,p)) then goto parse_attribute_name end
		if sbyte(s,p) == GT then p=p+1 goto parse_text end
		if sbyte(s,p) == SLASH and sbyte(s,p+1) == GT then
			emit_close_tag()
			p=p+2
			goto parse_text
		end
		return nil, "syntax error in attributes"
	end

	::parse_attribute_name::
	do
		mark = p
		while isname(sbyte(s,p)) do p=p+1 end
		att = ssub(s, mark, p-1)
		while iswhite(sbyte(s,p)) do p=p+1 end
		if sbyte(s,p) == EQ then p=p+1 goto parse_attribute_value end
		return nil, "syntax error after attribute name"
	end

	::parse_attribute_value::
	do
		while iswhite(sbyte(s,p)) do p=p+1 end
		quote = sbyte(s,p)
		p=p+1
		if quote ~= QUOT and quote ~= APOS then return nil, "missing quote character" end
		mark = p
		while p <= n and sbyte(s,p) ~= quote do p=p+1 end
		if sbyte(s,p) == quote then
			emit_att(att, ssub(s, mark, p-1))
			p=p+1
			goto parse_attributes
		end
		return nil, "end of data in attribute value"
	end

	return nil, "the impossible happened"
end

local function print_xml(item)
	if type(item) == 'table' then
		io.write("<", item.tag)
		for k, v in pairs(item.attr) do
			io.write(" ", k, '="', escape(v), '"')
		end
		if #item > 0 then
			io.write(">")
			for i, v in ipairs(item) do
				print_xml(v)
			end
			io.write("</", item.tag, ">")
		else
			io.write("/>")
		end
	else
		io.write(escape(item))
	end
end

return { parse=parse_xml, print=print_xml }
