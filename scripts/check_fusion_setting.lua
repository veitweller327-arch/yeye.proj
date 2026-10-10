-- check_fusion_setting.lua - structural check of a Fusion .setting macro
-- without Fusion: valid Lua table, every connection/published input points to
-- an existing tool, every expression references an existing control.
-- Usage: lua5.1 scripts/check_fusion_setting.lua "fusion/Timing Shift Ghost.setting"
local path = arg[1]
local f = assert(io.open(path, "r"))
local src = f:read("*a"); f:close()

-- Stubs for Fusion's table constructors: each returns its table tagged with a type.
local env = {}
local function ctor(name) return function(t) t = t or {}; t.__type = name; return t end end
setmetatable(env, { __index = function(_, k) return ctor(k) end })
env.ordered = function() return function(t) return t end end
local chunk = assert(loadstring("return " .. src, path))
setfenv(chunk, env)
local root = chunk()

local fails = 0
local function fail(msg) print("  FAILED: " .. msg); fails = fails + 1 end

local macro
for name, t in pairs(root.Tools) do if t.__type == "MacroOperator" then macro = t; print("macro: " .. name) end end
if not macro then fail("no MacroOperator"); os.exit(1) end
local tools = macro.Tools
local function hasInput(toolName, inputName)
  local t = tools[toolName]
  if not t then return false end
  if t.Inputs and t.Inputs[inputName] then return true end
  if t.UserControls and t.UserControls[inputName] then return true end
  return inputName == "Output" or inputName == "Input"
end

-- Published inputs / outputs
if not macro.Inputs.MainInput1 then fail("MainInput1 missing (needed for Edit-page effects)") end
if not macro.Outputs.MainOutput1 then fail("MainOutput1 missing") end
local published = {}
for k, v in pairs(macro.Inputs) do
  if not hasInput(v.SourceOp, v.Source) then fail(k .. " -> " .. tostring(v.SourceOp) .. "." .. tostring(v.Source)) end
  if v.Name then published[#published + 1] = v.Name end
end
for k, v in pairs(macro.Outputs) do
  if not tools[v.SourceOp] then fail(k .. " -> missing tool " .. tostring(v.SourceOp)) end
end
table.sort(published)
print("published controls: " .. table.concat(published, ", "))

-- Connections and expressions inside the macro
for tname, t in pairs(tools) do
  for iname, inp in pairs(t.Inputs or {}) do
    if inp.SourceOp and not tools[inp.SourceOp] then fail(tname .. "." .. iname .. " -> missing tool " .. inp.SourceOp) end
    if inp.Expression then
      for ref_tool, ref_ctrl in inp.Expression:gmatch("([%a_][%w_]*)%.([%a_][%w_]*)") do
        if not hasInput(ref_tool, ref_ctrl) then fail(tname .. "." .. iname .. " expression references " .. ref_tool .. "." .. ref_ctrl) end
      end
      -- bare names (same-tool references), excluding functions/constants
      local stripped = inp.Expression:gsub("[%a_][%w_]*%.[%a_][%w_]*", "")
      for word in stripped:gmatch("[%a_][%w_]*") do
        local known = { Point = 1, cos = 1, sin = 1, pi = 1 }
        if not known[word] and not hasInput(tname, word) then fail(tname .. "." .. iname .. " expression uses unknown " .. word) end
      end
    end
  end
end
if fails > 0 then print(fails .. " problem(s)"); os.exit(1) end
print("FUSION SETTING OK")
