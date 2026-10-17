-- For the window test (run-frontend.sh): run the project inside Chimera's
-- window for a fixed number of frames, then write what the machine shows: the
-- frame count, the picture's size and the SHA-256 of a memory domain, which
-- the script compares with Chimera's headless runner's for the same project.
--
-- The job comes from the file named by the MINIHAWK_JOB variable:
--   frames=<how many frames to advance>
--   meta=<where to write the result>
--   domain=<the memory domain to hash, or nothing>
-- SPDX-License-Identifier: MIT

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local meta = {}
local function finish(status, detail)
	local lines = {
		"status=" .. status,
		"detail=" .. (detail or ""),
		"frames=" .. (meta.frames or 0),
		"domainsize=" .. (meta.size or 0),
		"domainhash=" .. (meta.hash or ""),
		"width=" .. (meta.width or 0),
		"height=" .. (meta.height or 0),
	}
	if meta.metaPath then
		writeAll(meta.metaPath, table.concat(lines, "\n") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("MINIHAWK_JOB")
if jobPath == nil then
	error("MINIHAWK_JOB env var not set")
end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.metaPath = job.meta

if emu.getsystemid() ~= "J2ME" then
	finish("ERROR", "wrong system id: " .. tostring(emu.getsystemid()))
end
if emu.getcorename() ~= "FreeJ2ME-Plus" then
	finish("ERROR", "wrong core: " .. tostring(emu.getcorename()))
end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

local frames = tonumber(job.frames) or 120
for _ = 1, frames do
	emu.frameadvance()
end

meta.frames = emu.framecount()
meta.height = client.bufferheight()
meta.width = client.bufferwidth()

local DOMAIN = job.domain
if DOMAIN ~= nil and DOMAIN ~= "" then
	local ok, why = pcall(function()
		memory.usememorydomain(DOMAIN)
		meta.size = memory.getcurrentmemorydomainsize()
		meta.hash = memory.hash_region(0, meta.size, DOMAIN)
	end)
	if not ok then
		finish("ERROR", "memory domain " .. DOMAIN .. ": " .. tostring(why))
	end
end

finish("OK", "")
