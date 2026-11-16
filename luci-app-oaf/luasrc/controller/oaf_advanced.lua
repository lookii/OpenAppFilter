module("luci.controller.oaf_advanced", package.seeall)

local util = require "luci.util"

function index()
	entry({"admin", "services", "oaf", "advanced"}, template("oaf/advanced"), _("Settings"), 90).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "get_system_info"}, call("get_system_info"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "set_system_info"}, call("set_system_info"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "get_work_mode"}, call("get_work_mode"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "set_work_mode"}, call("set_work_mode"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "get_tcp_rst"}, call("get_tcp_rst"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "set_tcp_rst"}, call("set_tcp_rst"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "get_advanced_settings"}, call("get_advanced_settings"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "set_advanced_settings"}, call("set_advanced_settings"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "export_all_settings"}, call("export_all_settings"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "import_all_settings"}, call("import_all_settings"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "get_url_record_settings"}, call("get_url_record_settings"), nil).leaf = true
	entry({"admin", "services", "oaf", "api", "system", "set_url_record_settings"}, call("set_url_record_settings"), nil).leaf = true
end

-- 备份/恢复只处理这些 UCI 配置文件。上网审计数据（client.db、
-- client_data/、global/、/tmp 下的状态与缓存）不在此列，天然被排除。
local BACKUP_FILES = {
	"appfilter", "appfilter_whitelist", "macfilter", "macfilter_whitelist",
	"mac_blacklist", "fwx", "fwx_record", "user_info", "urlfilter"
}

local function backup_allowed(name)
	for _, n in ipairs(BACKUP_FILES) do
		if n == name then return true end
	end
	return false
end

local function ubus_call(api, payload)
	payload = payload or {}
	payload.api = api
	return util.ubus("fwx", "common", payload) or {code = 1}
end

function get_system_info()
	local http = require "luci.http"
	local resp = ubus_call("get_system_info", {data = {}})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function set_system_info()
	local http = require "luci.http"
	local lan_ifname = http.formvalue("lan_ifname") or ""
	local theme_mode = tonumber(http.formvalue("theme_mode") or "0") or 0

	if theme_mode ~= 0 and theme_mode ~= 1 then
		theme_mode = 0
	end

	local resp = ubus_call("set_system_info", {
		data = {
			fwx = {
				lan_ifname = lan_ifname,
				theme_mode = theme_mode
			}
		}
	})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function get_work_mode()
	local http = require "luci.http"
	local resp = ubus_call("get_work_mode", {data = {}})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function set_work_mode()
	local http = require "luci.http"
	local work_mode = tonumber(http.formvalue("work_mode") or "0") or 0

	if work_mode ~= 0 and work_mode ~= 1 then
		http.prepare_content("application/json")
		http.write_json({code = 1})
		return
	end

	local resp = ubus_call("set_work_mode", {
		data = {
			work_mode = work_mode
		}
	})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function get_tcp_rst()
	local http = require "luci.http"
	local resp = ubus_call("get_tcp_rst", {data = {}})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function set_tcp_rst()
	local http = require "luci.http"
	local tcp_rst = tonumber(http.formvalue("tcp_rst") or "1") or 1

	tcp_rst = tcp_rst == 0 and 0 or 1

	local resp = ubus_call("set_tcp_rst", {
		data = {
			tcp_rst = tcp_rst
		}
	})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function get_advanced_settings()
	local http = require "luci.http"
	local resp = ubus_call("get_advanced_settings", {data = {}})

	http.prepare_content("application/json")
	http.write_json(resp)
end

function set_advanced_settings()
	local http = require "luci.http"
	local disable_hnat = tonumber(http.formvalue("disable_hnat") or "0") or 0

	disable_hnat = disable_hnat == 1 and 1 or 0

	-- show_visit_url 的开关已从界面移除（与「URL访问记录」总开关重复）。
	-- 仅在显式传参时下发，避免旧页面或脚本调用把已存值清成 0。
	local data = { disable_hnat = disable_hnat }
	local show_visit_url = http.formvalue("show_visit_url")
	if show_visit_url ~= nil then
		data.show_visit_url = (tonumber(show_visit_url) == 1) and 1 or 0
	end

	local resp = ubus_call("set_advanced_settings", { data = data })

	http.prepare_content("application/json")
	http.write_json(resp)
end

-- 导出整个 oaf 设置。直接保存 UCI 文件原文，可 100% 保留命名 section、
-- list 顺序与注释，导入后 oafd 的规则轮询会在数秒内重新下发。
function export_all_settings()
	local http = require "luci.http"
	local nfs = require "nixio.fs"
	local files = {}

	for _, name in ipairs(BACKUP_FILES) do
		local path = "/etc/config/" .. name
		if nfs.access(path) then
			files[name] = nfs.readfile(path) or ""
		end
	end

	http.prepare_content("application/json")
	http.write_json({
		code = 0,
		data = {
			format = "oaf-settings",
			version = 1,
			exported_at = os.time(),
			files = files
		}
	})
end

function import_all_settings()
	local http = require "luci.http"
	local nfs = require "nixio.fs"
	local jsonc = require "luci.jsonc"

	http.prepare_content("application/json")

	local raw = http.formvalue("data") or ""
	local ok, obj = pcall(jsonc.parse, raw)
	if not ok or type(obj) ~= "table" then
		http.write_json({code = 1, message = "Invalid backup data"})
		return
	end

	local payload = obj
	if type(obj.data) == "table" and type(obj.data.files) == "table" then
		payload = obj.data
	end
	if payload.format ~= nil and payload.format ~= "oaf-settings" then
		http.write_json({code = 1, message = "Unsupported backup format"})
		return
	end

	local files = payload.files
	if type(files) ~= "table" or next(files) == nil then
		http.write_json({code = 1, message = "No configuration files in backup"})
		return
	end
	for name, content in pairs(files) do
		if not backup_allowed(name) or type(content) ~= "string" then
			http.write_json({code = 1, message = "Invalid backup entry"})
			return
		end
	end

	local stamp = tostring(os.time())
	local backup_dir = "/tmp/oaf-config-backup-" .. stamp
	pcall(function() nfs.mkdir(backup_dir) end)

	local imported = 0
	for name, content in pairs(files) do
		local path = "/etc/config/" .. name
		if nfs.access(path) then
			pcall(function()
				nfs.writefile(backup_dir .. "/" .. name, nfs.readfile(path))
			end)
		end
		if nfs.writefile(path, content) then
			imported = imported + 1
		end
	end

	http.write_json({
		code = imported > 0 and 0 or 1,
		data = {imported = imported, backup = backup_dir}
	})
end

function get_url_record_settings()
	local utl = require "luci.util"
	luci.http.prepare_content("application/json")
	local req = { api = "get_url_record_settings", data = {} }
	local resp = utl.ubus("fwx", "common", req)
	if resp and (resp.code == 2000 or resp.code == 0) then
		luci.http.write_json({code = 0, data = resp.data or {}})
	else
		luci.http.write_json({code = 1, message = "failed", data = {}})
	end
end

function set_url_record_settings()
	local utl = require "luci.util"
	local json = require "luci.jsonc"
	luci.http.prepare_content("application/json")
	local d = json.parse(luci.http.formvalue("data") or "{}")
	if type(d) ~= "table" then d = {} end
	local req = { api = "set_url_record_settings", data = d }
	local resp = utl.ubus("fwx", "common", req)
	if resp and (resp.code == 2000 or resp.code == 0) then
		luci.http.write_json({code = 0, data = resp.data or {}})
	else
		luci.http.write_json({code = 1, message = "failed", data = {}})
	end
end
