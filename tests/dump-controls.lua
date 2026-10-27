-- The bindings the machine has, written for a core's tests to compare with
-- the keys its package ships (check-keybinds.py). Chimera's window keeps only
-- the keys a person changed, so with none changed these are the package's.
-- The job file named by MINIHAWK_JOB (or CHIMERA_JOB) says where:
--   out=<the bindings, JSON>   meta=<status=OK when written>
local jobPath = os.getenv("MINIHAWK_JOB") or os.getenv("CHIMERA_JOB")
if jobPath == nil then error("MINIHAWK_JOB env var not set") end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
local f = assert(io.open(job.out, "w"))
f:write(chimera.controls.effective())
f:close()
if job.meta then
	local m = assert(io.open(job.meta, "w"))
	m:write("status=OK\n")
	m:close()
end
client.exit()
