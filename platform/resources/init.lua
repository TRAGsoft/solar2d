------------------------------------------------------------------------------
--
-- This file is part of the Corona game engine.
-- For overview and more information on licensing please refer to README.md 
-- Home page: https://github.com/coronalabs/corona
-- Contact: support@coronalabs.com
--
------------------------------------------------------------------------------

-- Tell luacheck not to flag our builtin globals
-- luacheck: globals system
-- luacheck: globals display
-- luacheck: globals native
-- luacheck: globals network
-- luacheck: globals Runtime
-- luacheck: globals simulator

local function getOrCreateTable( receiver, index )
	local t = receiver[index]
	if nil == t then
		t = {}
		receiver[index] = t
	end
	return t
end

local Object = {}
function Object:new( o )
	o = o or {}

	setmetatable( o, self )
	self.__index = self

	return o
end

function Object:newClass( o )
	o = self:new(o)
	o._super = self
	return o
end

-------------------------------------------------------------------------------
-- EventDispatcher
-------------------------------------------------------------------------------

local EventDispatcher = Object:newClass()

EventDispatcher._indexForType = { ["table"]="_tableListeners", ["function"]="_functionListeners" }

function EventDispatcher:getOrCreateTable( eventName, listenerType )
	local index = EventDispatcher._indexForType[ listenerType ]
	local t = nil
	if index then
		t = getOrCreateTable( self, index )
		t = getOrCreateTable( t, eventName )
	end
	if nil == t then
        error("addEventListener: listener cannot be nil: "..tostring(index))
    end
	return t
end

-- Every instance of EventDispatcher has two tables of listeners stored in then
-- _tableListeners and _functionListeners properties. Each of these tables is
-- itself a table that stores arrays keyed by the eventName. For example, a
-- table listener for the event "touch" is stored as an element of the array
-- self._tableListeners["touch"]. We have convenience methods to access the
-- either then _tableListeners or _functionListeners properties.
--
-- Here's an example of an EventDispatcher instance "o" and the contents of its
-- _tableListeners and _functionListeners private properties:
--
--		o._tableListeners =
--		{
--			"touch" = { obj1, obj2, obj3, ... },
--			"enterFrame" = { obj4, obj5, obj6, ... },
--		}
--
--		o._functionListeners =
--		{
--			"touch" = { func1, func2, func3, ... },
--			"enterFrame" = { func4, func5, func6, ... },
--		}
--
function EventDispatcher:addEventListener( eventName, listener )
    -- assume 'self' if listener not specified
    if not listener and self[eventName] then listener = self; end

	-- get table for either function or table listeners
	local listenerType = type( listener )
	local t = self:getOrCreateTable( eventName, listenerType )
	if t then
		table.insert( t, listener )
	end
	return (t ~= nil)
end

-- luacheck: push
-- luacheck: ignore 212 -- Unused argument.
function EventDispatcher:didRemoveListener( eventName )
end
-- luacheck: pop

function EventDispatcher:removeEventListener( eventName, listener )
    -- assume 'self' if listener not specified
    if not listener and self[eventName] then listener = self; end

	-- Find the given event listener in this dispatcher's collection.
	local wasRemoved = false
	local k = EventDispatcher._indexForType[ type(listener) ]
	local listenerTable = self[ k ] -- table either for function or table listeners
	if listenerTable then
		local listenerArray = listenerTable[ eventName ]
		if listenerArray then
			for index = 1, #listenerArray do
				if rawequal( listener, listenerArray[index] ) then
					-- Remove the listener from the event's array of listeners.
					table.remove( listenerArray, index )
					wasRemoved = true

					-- If the event's array of listeners is empty, then remove array from dispatcher's collection.
					if #listenerArray == 0 then
						listenerTable[ eventName ] = nil
					end

					-- Signal an EventDispatcher derived object that a listener has been removed.
					self:didRemoveListener( eventName )
					break
				end
			end
		end
	end

	-- Returns true if the listener was found and removed.
	return wasRemoved or nil
end

function EventDispatcher:hasEventListener( eventName, listener )
    -- assume 'self' if listener not specified
    if not listener and self[eventName] then listener = self; end

	-- Find the given event listener in this dispatcher's collection.
	local k = EventDispatcher._indexForType[ type(listener) ]
	local listenerTable = self[ k ] -- table either for function or table listeners
	if listenerTable then
		local listenerArray = listenerTable[eventName]
		if listenerArray then
			for index = 1, #listenerArray do
				if rawequal( listener, listenerArray[index] ) then
					return true
				end
			end
		end
	end
	return false
end

function cloneArray( array )
	local clone = {}
	for k,v in ipairs( array ) do
		clone[k] = v
	end
	return clone
end

display._initProfiling()

local profiles, augmentedNameStash = {}, {}
local _type = type

local function lookupProfile( name )
	local id = profiles[name]

	if not id and _type( name ) == "string" then
		local aug = name .. " (event)"

		id = display._allocateProfile( aug )
		profiles[name] = id
		augmentedNameStash[#augmentedNameStash + 1] = aug -- prevent collection
	end

	return display._beginProfile( id )
end

function EventDispatcher:dispatchEvent( event )
	local result = false;
	local eventName = event.name
	local profile

	-- array of functions is self._functionListeners[eventName]
	local functionDict = self._functionListeners
	local functionArray = ( functionDict and functionDict[eventName] ) or nil
	if ( functionArray ~= nil ) and ( #functionArray > 0 ) then
	--	local profile = display._beginProfile( functionArray._profileID )
		profile = lookupProfile( eventName )

		local functionArrayClone = cloneArray( functionArray )
		for index = 1, #functionArrayClone do
			local func = functionArrayClone[ index ]
			if self:hasEventListener( eventName, func ) then
				-- Dispatch event to function listener.
				local handled = func( event )
				result = handled or result
			end
		end
	--	display._endProfile( profile )
	end

	-- array of table listeners is self._tableListeners[eventName]
	local tableDict = self._tableListeners
	local tableArray = ( tableDict and tableDict[eventName] ) or nil
	if ( tableArray ~= nil ) and ( #tableArray > 0 ) then
	--	local profile = display._beginProfile( tableArray._profileID )
		profile = profile or lookupProfile( eventName )

		local tableArrayClone = cloneArray( tableArray )
		for index = 1, #tableArrayClone do
			local obj = tableArrayClone[ index ]
			if self:hasEventListener( eventName, obj ) then
				-- Fetch method stored as property of object.
				local method = obj[eventName]
				if ( type(method) == "function" ) then
					-- Dispatch event to table listener.
					local handled = method( obj, event )
					result = handled or result
				end
			end
		end
	--	display._endProfile( profile )
	end
	
	display._endProfile( profile )

	return result
end

function EventDispatcher:respondsToEvent( eventName )
	local t = self._functionListeners
	local result = t and t[eventName]

	if not result then
		t = self._tableListeners
		result = t and t[eventName]
	end

	return result;
end

-- Set up a public function allowing developers to create their own private event dispatchers.
-- Mostly intended for plugin developers. Avoids event name collision with Runtime's event dispatcher.
system.newEventDispatcher = function()
	return EventDispatcher:new()
end

-------------------------------------------------------------------------------
-- Runtime
-------------------------------------------------------------------------------

Runtime = EventDispatcher:newClass()

Runtime._proxy =
{
	__index = system.__proxyindex,
	__newindex = system.__proxynewindex
}

local eventsRequiringNativeListener = { orientation=true, accelerometer=true, gyroscope=true, engineLog=true }

function Runtime:addEventListener( eventName, listener )
	local super = self._super
	local noListeners = not self:respondsToEvent( eventName )
	local wasAdded = super.addEventListener( self, eventName, listener )

	if ( noListeners ) then
		if ( eventsRequiringNativeListener[ eventName ] ) then
			system.beginListener( eventName )
		end
	end
	return wasAdded or nil
end

function Runtime:didRemoveListener( eventName )
	if ( not self:respondsToEvent( eventName ) ) then
		if ( eventsRequiringNativeListener[ eventName ] ) then
			system.endListener( eventName )
		end
	end
end

function Runtime:removeEventListener( eventName, listener )
	local super = self._super
	return super.removeEventListener( self, eventName, listener )
end

-- luacheck: push
-- luacheck: ignore 212 -- Unused argument.
function Runtime:hasEventSource( eventName )
	return system.hasEventSource( eventName )
end

-------------------------------------------------------------------------------
-- Per-frame logic
-------------------------------------------------------------------------------

local _frame_id, _frame_start_time

local function onEnterFrameEvent( event )
	_frame_id, _frame_start_time = event.frame, event.time
end

Runtime:addEventListener( "enterFrame", onEnterFrameEvent )

function Runtime.getFrameID( )
	return _frame_id + 1
end

function Runtime.getFrameStartTime( )
	return _frame_start_time
end

-------------------------------------------------------------------------------
-- DisplayObject
-------------------------------------------------------------------------------

local DisplayObject = EventDispatcher:new()

system.__proxyregister( "DisplayObject", DisplayObject )

function DisplayObject:addEventListener( eventName, listener )
	local noListeners = not self:respondsToEvent( eventName )
	local wasAdded = EventDispatcher.addEventListener( self, eventName, listener )

	if ( noListeners ) then
		self:_setHasListener( eventName, true )
	end
	return wasAdded or nil
end

function DisplayObject:removeEventListener( eventName, listener )
	local wasRemoved = EventDispatcher.removeEventListener( self, eventName, listener )

	if ( not self:respondsToEvent( eventName ) ) then
		self:_setHasListener( eventName, false )
	end
	return wasRemoved or nil
end

-------------------------------------------------------------------------------
-- display
-------------------------------------------------------------------------------
-- convenience wrapper for object:removeSelf() that eliminates check for a receiver that's nil
display.remove = function( object )
	if object then
		local method = object.removeSelf
		if "function" == type( method ) then
			method( object ) -- same as object:removeSelf()
		end
	end
end

-------------------------------------------------------------------------------
-- luacheck: push
-- luacheck: ignore 111 -- Setting an undefined global variable.

-------------------------------------------------------------------------------
-- Easing
-------------------------------------------------------------------------------

easing = require "easing"

-- luacheck: pop
--------------------------------------------------------------------------------

local collectOrphans = display._collectOrphans
local collectgarbage_original = collectgarbage

-- luacheck: push
-- luacheck: ignore 121 -- Setting a read-only global variable.

-- wrap original collectgarbage with newer version
collectgarbage = function( opt, arg )
	-- map to original collectgarbage options
	local opt_lua = opt
	if "collectlua" == opt then
		opt_lua = "collect"
	end

	-- call original collectgarbage
	local result = collectgarbage_original( opt_lua, arg )

	-- new version also collects orphans
	if "collect" == opt or "collectorphans" == opt then
		collectOrphans()
	end

	return result
end

-- luacheck: pop

-- prevent public access to private function
display._collectOrphans = nil

--------------------------------------------------------------------------------

-- Load the "network" library on startup and provide it as a global.
-- This is needed since Corona's initialization code expects this functionality to be available on startup.
-- Note: Wrap it in a pcall() in case the network library has been removed or is unavailble.
network = nil
pcall( function() network = require "network" end )

--[[
network.setStatusListener = function( address, listener )
	local statusListeners = network._statusListeners

	if ( not listener ) then
		-- The Lua GC will implicitly finalize the userdata
		statusListeners[address] = nil
	elseif ( type(address) == "string" ) then
		if ( listener ) then
			local reachability = network._setStatusListener( address )
			if ( reachability ) then
				-- Store a reference to the reachability userdata so its lifetime matches that of the registered listener
				local data = { listener=listener, userdata=reachability }
				statusListeners[address] = data
			end
		end
	end
end

network._dispatchStatus = function( address, event )
	local data = network._statusListeners[address]
	if data then
		local listener = data.listener
		callListener( listener, event.name, event )
	end
end
--]]

------------------------------------------------------------------------------------
	-- Start: native.VideoView section
------------------------------------------------------------------------------------

-- VideoViews need to be paused on suspend and resumed on application resume.
-- Note: These variables are also used by native.newVideo override section.
local _weakTableOfAllVideos = {}
local _weakTableOfAllSuspendedVideos = {}
do
	local s_weakmetatable = {}
	s_weakmetatable.__mode = "kv"
	setmetatable(_weakTableOfAllVideos, s_weakmetatable)
	setmetatable(_weakTableOfAllSuspendedVideos, s_weakmetatable)
end

local function ClearSuspendedVideoList()
	-- Do not set the table to {} because I want to reuse the existing table.
	-- Otherwise, I will lose the metatable that specifies the weak-property
	for k, _ in pairs(_weakTableOfAllSuspendedVideos) do
		_weakTableOfAllSuspendedVideos[k] = nil
	end
end

local function SuspendAllPlayingVideos()
	-- erase all entries from the suspended video list to clean it up from possible previous usage
	ClearSuspendedVideoList()

	for k, _ in pairs(_weakTableOfAllVideos) do
		if not k.isPaused then
			-- when object:removeSelf() is called, all we have is a plain Lua table
			-- so check "pause" property to make sure it's a function
			if type( k.pause ) == "function" then
				_weakTableOfAllSuspendedVideos[k] = k
				k:pause()
			else
				-- remove reference to native video object that no longer exists
				_weakTableOfAllVideos[k] = nil
			end
		end
	end
end

local function ResumeAllSuspendedVideos()
	for  k, _ in pairs(_weakTableOfAllSuspendedVideos) do
		if type( k.play ) == "function" then
			k:play()
		end
	end

	-- List is now stale. Clean up
	ClearSuspendedVideoList()
end

------------------------------------------------------------------------------------
	-- End: native.VideoView section
------------------------------------------------------------------------------------

-- luacheck: push
-- luacheck: ignore 542 -- An empty if branch.

local function onInternalSystemEvent( event )
	if "applicationSuspend" == event.type then
		SuspendAllPlayingVideos()
	elseif "applicationResume" == event.type then
		ResumeAllSuspendedVideos()
	end
end

-- luacheck: pop

Runtime:addEventListener( "system", onInternalSystemEvent )

--------------------------------------------------------------------------------
-- Override/modify standard Lua functions
--------------------------------------------------------------------------------

-- Table to save old functions in case we need them
local _coronaPreservedLuaFunctions = {}
local _coronaBuildSettings
local _coronaBuildSettingsPath

-- Override 'print' because of Android and Apple suppressing stdout/stderr.
-- luacheck: push
-- luacheck: ignore

_coronaPreservedLuaFunctions.print = print
print = coronabaselib.print

-- luacheck: pop

-- luacheck: push
-- luacheck: ignore 121 -- Setting a read-only global variable.

_coronaPreservedLuaFunctions.require = require
require = function (modname)
	if ( "simulator" == system.getInfo( "environment" ) ) or ( "win32" == system.getInfo( "platform" ) ) or ( "macos" == system.getInfo( "platform" ) ) then
		-- Replace '.' with '_' for the following cases:
		-- (1) For plugins
		-- (2) For CoronaProvider subclasses, e.g. CoronaProvider.native.popup.quickLook.
		-- Note that this does _not_ affect base provider classes (CoronaProvider.native)
		local prefix = "plugin."
		if ( string.sub( modname, 1, string.len( prefix ) ) == prefix )
			or ( nil ~= string.match( modname, 'CoronaProvider%.(.*)%.(.*)' ) ) then

			-- Only check for missing plugin config in the Simulators and
			-- not for CoronaProviders
			if ( "simulator" == system.getInfo( "environment" )
				and not string.starts(modname, "CoronaProvider.") ) then
				-- load build.settings so we can see if the plugin is configured
				-- (by caching the path we only load it once per app start)
				local path = system.pathForFile("build.settings", system.ResourceDirectory)
				if path and path ~= _coronaBuildSettingsPath then
					local fp = io.open(path, 'r')

					if fp then
						-- CoronaSDK doesn't expose loadfile() so we need to do it the hard way
						local lua = fp:read( '*a' )
						fp:close()

						local buildSettings = loadstring(lua)
						buildSettings() -- creates "settings" table
						_coronaBuildSettings = settings
						settings = nil
					end

					_coronaBuildSettingsPath = path
				end

				if _coronaBuildSettings == nil then
					_coronaBuildSettings = {}
				end

				if _coronaBuildSettings.plugins == nil then
					_coronaBuildSettings.plugins = {}
				end

				if _coronaBuildSettings.plugins[modname] == nil then
					-- check that this isn't a submodule of an already loaded plugin
					-- i.e. do not warn if this is "plugin.name.subname" and "plugin.name" is loaded
					local guardedRequredName = modname .. '.'
					local submodule = false
					for key, _ in pairs(_coronaBuildSettings.plugins) do
						local guardedSettingsName = key .. "." -- added to stop incorrect substring matches
						if guardedRequredName:starts(guardedSettingsName) or guardedSettingsName:starts(guardedRequredName) then
							submodule = true
							break
						end
					end

					if not submodule then
						local output = "WARNING: "..modname.." is not configured in build.settings"
						-- emit a simplified stack trace (it turns out that all the relevant
						-- info is embodied in the lines ending with "in main chunk")
						output = output .. "\nstack traceback:\n"
						local stackdesc = debug.traceback()
						for line in stackdesc:gmatch("[^\r\n]+") do 
							if string.ends(line, "in main chunk") then
								output = output .. line .. "\n"
							end
						end
						print(output)
					end
				end
			end

			-- Default to original require behavior first first, so `require "plugin.X"`
			-- does the standard thing (convert . to /) which allows subdirectory lookup
			-- to occur. This is useful for pure-Lua plugin developers.
			local result, mod = pcall( _coronaPreservedLuaFunctions.require, modname )
			if ( result ) then
				return mod -- traditional require works fine
			elseif ( "string" == type( mod ) ) then
				-- Determine if this was a Lua syntax error
				if string.starts( mod, "error loading module" ) then
					error( mod, 2 )
				end
			end

			-- To support Lua plugins for Simulator that are hosted on server,
			-- we modify the modname param (convert . to _):
			modname = string.gsub( modname, '%.', '_' )
		end
	end
	return _coronaPreservedLuaFunctions.require( modname )
end

-- luacheck: pop

--------------------------------------------------------------------------------
-- Override/modify Corona Lua functions
--------------------------------------------------------------------------------

_coronaPreservedLuaFunctions.native = _coronaPreservedLuaFunctions.native or {}
_coronaPreservedLuaFunctions.native.canShowPopup = native.canShowPopup
_coronaPreservedLuaFunctions.native.showPopup = native.showPopup
_coronaPreservedLuaFunctions.system = _coronaPreservedLuaFunctions.system or {}
_coronaPreservedLuaFunctions.system.getInfo = system.getInfo

native._getProvider = function( category, name )
	if not name then
		return nil
	end

	local modName = "CoronaProvider.native." .. category .. "." .. name
	local success, result = pcall( require, modName )

	if not success then
		result = nil
	end

	return result
end

native.canShowPopup = function( ... )
	local result = _coronaPreservedLuaFunctions.native.canShowPopup( ... )

	if not result then
		local name = ...
		local provider = native._getProvider( "popup", name )
		if provider then
			result = provider.canShowPopup( ... )
		end
	end

	return result
end

native.showPopup = function( ... )
	local result = false

	local name = ...
	if _coronaPreservedLuaFunctions.native.canShowPopup( name ) then
		result = _coronaPreservedLuaFunctions.native.showPopup( ... )
	else
		local provider = native._getProvider( "popup", name )
		if provider and "function" == type( provider.showPopup ) then
			result = provider.showPopup( ... )
		end
	end

	return result
end

system.getInfo = function( ... )
	-- Perform custom system.getInfo() handling here.
	local keyName = ...
	if (type(keyName) == "string") then
		if ("isoLanguageCode" == keyName) then
			-- *** Make sure that the returned ISO language code is handled consistently on all platforms. ***

			-- Fetch the system's currently selected language as an ISO 639 language code.
			local languageCode = _coronaPreservedLuaFunctions.system.getInfo(...)
			if (type(languageCode) ~= "string") then
				return ""
			end

			-- Make sure the language code is lowercase on all platforms.
			languageCode = string.lower(languageCode)

			-- If an ISO 15924 script code was postfixed to the end of string, such as "zh-hans" or "zh-hant",
			-- then make sure it uses a dash as the separator instead of an underscore for consistency.
			languageCode = string.gsub(languageCode, "%_", "-")

			-- Special handling for Chinese language.
			-- Make sure it always ends with an ISO 15924 script code such as "hans" or "hant".
			if (string.starts(languageCode, "zh")) then
				if (string.ends(languageCode, "hans") == false) and (string.ends(languageCode, "hant") == false) then
					-- Script code not found. Determine which to use based on country code.
					-- Note: Chinese Simplified is used in China and Singapore.
					--       All other countries/regions are assumed to use Chinese Traditional.
					local countryCode = _coronaPreservedLuaFunctions.system.getInfo("isoCountryCode")
					countryCode = string.lower(countryCode)
					if (countryCode == "cn") or (countryCode == "sg") then
						languageCode = languageCode .. "-hans"
					else
						languageCode = languageCode .. "-hant"
					end
				end
			end
			return languageCode
		elseif ("isoCountryCode" == keyName) then
			-- *** Make sure that the returned ISO country code is handled consistently on all platforms. ***

			-- Fetch the system's currently selected region as an ISO 3166-1 country code.
			local countryCode = _coronaPreservedLuaFunctions.system.getInfo(...)
			if (type(countryCode) ~= "string") then
				return ""
			end

			-- Make sure the country code is lowercase on all platforms.
			countryCode = string.lower(countryCode)
			return countryCode
		end
	end

	-- The given key name was not overriden by this module. Let the core function handle it.
	return _coronaPreservedLuaFunctions.system.getInfo( ... )
end

--------------------------------------------------------------------------------
-- Simulator authoring controls
--------------------------------------------------------------------------------

if "simulator" == system.getInfo("environment") then
	simulator = require("_simulator")

	-- Screen recording is asynchronous. The native API dispatches a
	-- "screenRecording" Runtime event; this wrapper offers the more convenient
	-- per-recording listener form while preserving global Runtime listeners.
	local nativeStartScreenRecording = simulator.startScreenRecording
	simulator.startScreenRecording = function(options, listener)
		if listener ~= nil and type(listener) ~= "function" and
			(type(listener) ~= "table" or type(listener.screenRecording) ~= "function")
		then
			error("simulator.startScreenRecording() listener must be a function or table listener", 2)
		end

		local accepted, message = nativeStartScreenRecording(options)
		if accepted and listener ~= nil then
			local runtimeListener
			runtimeListener = function(event)
				if event.phase == "ended" or event.phase == "failed" then
					Runtime:removeEventListener("screenRecording", runtimeListener)
				end
				if type(listener) == "function" then
					return listener(event)
				end
				return listener:screenRecording(event)
			end
			Runtime:addEventListener("screenRecording", runtimeListener)
		end
		return accepted, message
	end
end
--------------------------------------------------------------------------------
-- Startup Logging
--------------------------------------------------------------------------------

-- Output support info to debug log
-- "model" is "(null)" for Simulator windows and we don't need to report for those
if system.getInfo("model") ~= "(null)"
then
    print("Platform: " ..tostring(system.getInfo("model")) .." / "
        ..tostring(system.getInfo("architectureInfo")) .." / "
        ..tostring(system.getInfo("platformVersion")) .." / "
        ..tostring(system.getInfo("GL_RENDERER")) .." / "
        ..tostring(system.getInfo("GL_VERSION")) .." / "
        ..tostring(system.getInfo("build")) .." / "
        ..tostring(system.getPreference("ui", "language")) .." | "
        ..tostring(system.getPreference("locale", "country")) .." | "
        ..tostring(system.getPreference("locale", "identifier")) .." | "
        ..tostring(system.getPreference("locale", "language")))

end

-- Provide a way to access the real globals table (used to be needed when we overrode _G and remains
-- for backwards compatibility)
Runtime._G = _G
