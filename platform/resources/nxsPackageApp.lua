--
-- This file is part of the Corona game engine.
-- For overview and more information on licensing please refer to README.md 
-- Home page: https://github.com/coronalabs/corona
-- Contact: support@coronalabs.com
--
processExecute = processExecute or os.execute
local lfs = require "lfs"
local json = require "json"
local http = require( "socket.http" )
local dirSeparator = package.config:sub(1,1)
local windows = (dirSeparator == '\\')
local buildSettings = nil
local function log(...)
    myprint(...)
end
local function logd(...)
    myprint(...)
end
local function quoteString( str )
    str = str:gsub('\\', '\\\\')
    str = str:gsub('"', '\\"')
    return "\"" .. str .. "\""
end
local function globToPattern(g)
  local p = "^"
  local i = 0
  local c
  local function unescape()
    if c == '\\' then
      i = i + 1; c = g:sub(i,i)
      if c == '' then
        p = '[^]'
        return false
      end
    end
    return true
  end
  local function escape(c)
    return c:match("^%w$") and c or '%' .. c
  end
  local function charset_end()
    while 1 do
      if c == '' then
        p = '[^]'
        return false
      elseif c == ']' then
        p = p .. ']'
        break
      else
        if not unescape() then break end
        local c1 = c
        i = i + 1; c = g:sub(i,i)
        if c == '' then
          p = '[^]'
          return false
        elseif c == '-' then
          i = i + 1; c = g:sub(i,i)
          if c == '' then
            p = '[^]'
            return false
          elseif c == ']' then
            p = p .. escape(c1) .. '%-]'
            break
          else
            if not unescape() then break end
            p = p .. escape(c1) .. '-' .. escape(c)
          end
        elseif c == ']' then
          p = p .. escape(c1) .. ']'
          break
        else
          p = p .. escape(c1)
          i = i - 1
        end
      end
      i = i + 1; c = g:sub(i,i)
    end
    return true
  end
  local function charset()
    i = i + 1; c = g:sub(i,i)
    if c == '' or c == ']' then
      p = '[^]'
      return false
    elseif c == '^' or c == '!' then
      i = i + 1; c = g:sub(i,i)
      if c == ']' then
      else
        p = p .. '[^'
        if not charset_end() then return false end
      end
    else
      p = p .. '['
      if not charset_end() then return false end
    end
    return true
  end
  while 1 do
    i = i + 1; c = g:sub(i,i)
    if c == '' then
      p = p .. '$'
      break
    elseif c == '?' then
      p = p .. '.'
    elseif c == '*' then
      p = p .. '.*'
    elseif c == '[' then
      if not charset() then break end
    elseif c == '\\' then
      i = i + 1; c = g:sub(i,i)
      if c == '' then
        p = p .. '\\$'
        break
      end
      p = p .. escape(c)
    else
      p = p .. escape(c)
    end
  end
  return p
end
local function pathJoin(p1, p2, ... )
    local res
    local p1s = p1:sub(-1) == dirSeparator
    local p2s = p2:sub(1, 1) == dirSeparator
    if p1s and p2s then
        res = p1:sub(1,-2) .. p2
    elseif p1s or p2s then
        res = p1 .. p2
    else
        res = p1 .. dirSeparator .. p2
    end
    if ... then
        return pathJoin(res, ...)
    else
        return res
    end
end
local function unzip( archive, dst, p )
    if windows then
        -- Resolve CORONA_PATH in Lua rather than relying on cmd.exe to expand
        -- %CORONA_PATH% on its own. processExecute -> CommandLineRunner returns
        -- -1 when the command starts with %VAR%-style expansion that cmd.exe
        -- evaluates inside the wrapping CreateProcess call (the same command
        -- works via `cmd /c` from a normal shell). Lua-resolved path avoids it.
        local coronaPath = os.getenv("CORONA_PATH") or ""
        local cmd = '""' .. coronaPath .. '\\7za.exe" x -aoa "' .. archive .. '" -o"' ..  dst .. '" ' .. (p or "") .. '"'
        -- NOTE: must use os.execute, not processExecute. processExecute on
        -- Windows wraps Interop::Ipc::CommandLineRunner which returns -1 for
        -- commands of this shape (leading ""path"...) even though cmd.exe
        -- accepts them just fine. os.execute -> C system() -> cmd /c works.
        local osResult = os.execute(cmd)
        return (osResult == 0 or osResult == true) and 0 or 1
    else
        return os.execute('/usr/bin/unzip -o -q ' .. quoteString(archive) .. ' -d ' ..  quoteString(dst))
    end
end
local function isDir(f)
    if not f then return false end
    return lfs.attributes(f, 'mode') == 'directory'
end
local function isFile(f)
    if not f then return false end
    return lfs.attributes(f, 'mode') == 'file'
end
local function getFileSize(f)
    if not f then return 0 end
    return lfs.attributes(f, 'size') or 0
end
local function copyFile(src, dst)
    local fi = io.open(src, "rb")
    if (fi) then
        local buf = fi:read("*a")
        fi:close()
        fi = io.open(dst, "wb")
        if (fi) then
            fi:write(buf)
            fi:close()
            return true
        end
        logd('copyFile: Failed to write ' .. dst)
        return false
    end
    logd('copyFile: Failed to read ' .. src)
    return false
end
local function copyDir( src, dst, extraExcludeDirs )
    if windows then
        -- See unzip() above: processExecute -> CommandLineRunner returns -1 for
        -- commands of this shape, masking copy failures as success. os.execute
        -- -> C system() -> cmd /c handles them correctly.
        -- /xd always excludes the destination itself: when dst nests inside src
        -- (in-project build output dir), robocopy would otherwise recurse into
        -- its own output forever. ".git" never belongs in a packaged build.
        local excludes = ' /xd "' .. dst .. '" ".git"'
        if extraExcludeDirs then
            for i = 1, #extraExcludeDirs do
                excludes = excludes .. ' "' .. extraExcludeDirs[i] .. '"'
            end
        end
        local cmd = 'robocopy "' .. src .. '" "' .. dst .. '" /e /nfl /ndl /njh /njs /nc /ns /np' .. excludes .. ' > nul 2> nul'
        local rc = os.execute(cmd)
        if type(rc) == 'boolean' then return rc and 0 or 1 end
        -- robocopy exit codes 0-7 are success; >=8 is failure.
        return (rc and rc < 8) and 0 or 1
    else
        local cmd = 'cp -R ' .. quoteString(src) .. '/. ' ..  quoteString(dst)
        return os.execute(cmd)
    end
end
local function removeDir( dir )
    if windows then
        local cmd = 'rmdir /s/q "' .. dir .. '" > nul 2> nul'
        return os.execute(cmd)
    else
        return os.execute("rm -f -r " .. quoteString(dir))
    end
end
-- Convert short 8.3 path to long path on Windows
-- This only works for paths that already exist!
local function getLongPath(path)
    if not windows then
        return path
    end
    
    -- Skip if path doesn't contain ~ (not a short path)
    if not path:find("~") then
        return path
    end
    
    -- Use PowerShell to get the long path
    local escapedPath = path:gsub("'", "''")
    local cmd = 'powershell -Command "(Get-Item -LiteralPath \'' .. escapedPath .. '\').FullName"'
    local rc, stdout = processExecute(cmd, true)
    
    if rc == 0 and type(stdout) == 'string' and #stdout > 0 then
        local result = stdout:gsub("[\r\n]+", "") -- trim newlines
        if #result > 0 then
            return result
        end
    end
    
    return path
end
-- Convert a path containing short names to long names
-- Works even if the final component doesn't exist yet (converts parent path)
local function getLongPathParent(path)
    if not windows then
        return path
    end
    
    -- Skip if path doesn't contain ~ (not a short path)
    if not path:find("~") then
        return path
    end
    
    -- Split into parent and final component
    local parent = path:match("(.+)\\[^\\]+$")
    local filename = path:match("\\([^\\]+)$")
    
    if parent and filename then
        -- Convert the parent path (which should exist)
        local longParent = getLongPath(parent)
        return longParent .. "\\" .. filename
    end
    
    return getLongPath(path)
end

local function nxsDownloadPlugins(buildRevision, tmpDir, pluginDstDir)
    if type(buildSettings) ~= 'table' then
        return nil
    end
    local collectorParams = {
        pluginPlatform = 'nx64',
        plugins = buildSettings.plugins or {},
        destinationDirectory = tmpDir,
        build = buildRevision,
        extractLocation = pluginDstDir,
    }
    local pluginCollector = require "CoronaBuilderPluginCollector"
    return pluginCollector.collect(collectorParams)
end
local function getExcludePredecate()
    local excludes = {
        "*.config",
        "*.lu",
        "*.lua",
        "*.bak",
        "*.orig",
        "*.swp",
        "*.DS_Store",
        "*.apk",
        "*.obb",
        "*.obj",
        "*.o",
        "*.lnk",
        "*.class",
        "*.log",
        "build.properties",
    }
    
    -- Files/folders to always keep (whitelist)
    local whitelist = {
        [".nrr"] = true,  -- NRR folder for NRO registration
    }
    
    if buildSettings and buildSettings.excludeFiles then
        if buildSettings.excludeFiles.all then
            for i = 1, #buildSettings.excludeFiles.all do
                excludes[#excludes + 1] = buildSettings.excludeFiles.all[i]
            end
        end
        if buildSettings.excludeFiles.nx then
            for i = 1, #buildSettings.excludeFiles.nx do
                excludes[#excludes + 1] = buildSettings.excludeFiles.nx[i]
            end
        end
    end
    for i = 1, #excludes do
        excludes[i] = globToPattern(excludes[i])
    end
    return function(fileName, relativePath)
        -- Always keep whitelisted items
        if whitelist[fileName] then
            return true
        end
        -- Exclude hidden files/folders (starting with dot) except whitelisted
        if fileName:sub(1, 1) == '.' then
            return false
        end
        -- Check exclude patterns
        for i = 1, #excludes do
            if fileName:find(excludes[i]) or relativePath:find(excludes[i]) then
                return false
            end
        end
        return true
    end
end
local function deleteUnusedFiles(srcDir, excludePredicate, relativeDir)
    relativeDir = relativeDir or ""
    for file in lfs.dir(srcDir) do
        -- Always skip . and .. first
        if file == '.' or file == '..' then
            -- skip
        else
            local f = pathJoin(srcDir, file)
            local relativePath = relativeDir == "" and file or relativeDir.."/"..file
            if excludePredicate(file, relativePath) then
                local attr = lfs.attributes(f)
                if attr and attr.mode == "directory" then
                    deleteUnusedFiles(f, excludePredicate, relativePath)
                end
            else
                local result
                if isDir(f) then
                    result = removeDir(f)
                else
                    result = os.remove(f)
                end
                if not result then
                    logd("Failed to exclude " .. f)
                end
            end
        end
    end
end
local function findNssFile(codeFolder)
    if not isDir(codeFolder) then
        return nil
    end
    for file in lfs.dir(codeFolder) do
        if file:lower():match("%.nss$") then
            return pathJoin(codeFolder, file)
        end
    end
    return nil
end
local function countFilesWithExtension(folder, ext)
    local count = 0
    local files = {}
    if isDir(folder) then
        for file in lfs.dir(folder) do
            if file ~= '.' and file ~= '..' and file:match("%." .. ext .. "$") then
                count = count + 1
                files[#files + 1] = file
            end
        end
    end
    return count, files
end
-- Check NSS file and log info for debugging
local function checkNssFile(nssPath)
    if not isFile(nssPath) then
        return "File does not exist", false
    end
    
    local size = getFileSize(nssPath)
    
    -- Read first bytes for debugging
    local f = io.open(nssPath, "rb")
    if not f then
        return "Cannot open file", false
    end
    
    local header = f:read(16)
    f:close()
    
    if not header then
        return "Cannot read file", false
    end
    
    -- Show first bytes as hex
    local hexStr = ""
    for i = 1, math.min(#header, 16) do
        hexStr = hexStr .. string.format("%02X ", header:byte(i))
    end
    
    -- Check for ELF magic: 0x7F 'E' 'L' 'F' (bytes: 127, 69, 76, 70)
    local isElf = false
    if #header >= 4 then
        local b1, b2, b3, b4 = header:byte(1, 4)
        isElf = (b1 == 0x7F and b2 == 0x45 and b3 == 0x4C and b4 == 0x46)
    end
    
    local info = string.format("Size: %d bytes, Magic: %s, IsELF: %s", size, hexStr, tostring(isElf))
    return info, isElf
end
--
-- global script to call from C++
--
function nxsPackageApp( args )
    local nxsRoot = args.sdkRoot
    if not nxsRoot or nxsRoot == "" then
        return "Missing sdkRoot packaging argument"
    end
    log('NX Switch App builder started')
    local nxInfo = args.nxInfo
    args.nxInfo = nil
    logd(json.prettify(args))
    local template = args.templateLocation
    logd("template location: " .. template)
    if not isFile(template) then
        return 'Missing template: ' .. template
    end
    -- read settings
    local buildSettingsFile = pathJoin(args.srcDir, 'build.settings')
    local oldSettings = _G['settings']
    _G['settings'] = nil
    pcall( function() dofile(buildSettingsFile) end )
    buildSettings = _G['settings']
    _G['settings'] = oldSettings
    local success = false
    
    -- Use a persistent working directory instead of temp
    -- The temp directory gets cleaned up too quickly, causing AuthoringTool to fail
    local tmpDir = pathJoin(args.dstDir, args.applicationName .. '_nxbuild')
    removeDir(tmpDir)
    success = lfs.mkdir(tmpDir)
    if not success then
        return 'Failed to create working directory: ' .. tmpDir
    end
    log('Using working directory: ' .. tmpDir)
    -- create app folder if it does not exists
    local nxsappFolder = pathJoin(args.dstDir, args.applicationName .. '.NX64')
    removeDir(nxsappFolder)
    success = lfs.mkdir(nxsappFolder)
    if not success then
        return 'Failed to create App folder: ' .. nxsappFolder
    end
    logd('AppFolder: ' .. nxsappFolder)
    local appFolder = pathJoin(tmpDir, 'nxsapp')
    removeDir(appFolder)
    success = lfs.mkdir(appFolder)
    if not success then
        return 'Failed to create tmp folder: ' .. appFolder
    end
    logd('appFolder: ' .. appFolder)
    -- Download plugins
    local pluginDownloadDir = pathJoin(tmpDir, "pluginDownloadDir")
    local pluginExtractDir = pathJoin(tmpDir, "pluginExtractDir")
    lfs.mkdir(pluginDownloadDir)
    lfs.mkdir(pluginExtractDir)
    local msg = nxsDownloadPlugins(args.buildRevision, pluginDownloadDir, pluginExtractDir)
    if type(msg) == 'string' then
        return msg
    end
    -- Copy lua plugins over to the app folder
    local luaPluginDir = pathJoin(pluginExtractDir, 'lua', 'lua_51')
    if isDir(luaPluginDir) then
        copyDir(luaPluginDir, pluginExtractDir)
        removeDir(pathJoin(pluginExtractDir, 'lua'))
    end
    luaPluginDir = pathJoin(pluginExtractDir, 'lua_51')
    if isDir(luaPluginDir) then
        copyDir(luaPluginDir, pluginExtractDir)
        removeDir(pathJoin(pluginExtractDir, 'lua_51'))
    end
    if isDir(pluginExtractDir) then
        copyDir(pluginExtractDir, appFolder)
    end
    -- unzip standard template
    local templateFolder = pathJoin(tmpDir, 'nxtemplate')
    removeDir(templateFolder)
    success = lfs.mkdir(templateFolder)
    if not success then
        return 'Failed to create template folder: ' .. templateFolder
    end
    logd('templateFolder: ' .. templateFolder)
    -- sanity check
    local archivesize = lfs.attributes(template, "size")
    if archivesize == nil or archivesize == 0 then
        return 'Failed to open template: ' .. template
    end
    local ret = unzip(template, templateFolder, nxInfo)
    if ret ~= 0 then
        return 'Failed to unpack template ' .. template .. ' to ' .. templateFolder ..  ', err=' .. ret
    end
    logd('Unzipped ' .. template .. ' to ' .. templateFolder)
    -- Define paths
    local codeFolder = pathJoin(templateFolder, 'code')
    log('Code folder: ' .. codeFolder)
    -- Fail fast on an unpublishable template BEFORE the expensive project copy
    -- and .car compile: publishable builds need an .nss in the template's code
    -- folder, and its absence cannot be fixed later in the build.
    local nssFile = findNssFile(codeFolder)
    if args.publishable then
        if not nssFile then
            return 'Publishable build requires an .nss file in the template code folder, but template ' .. template .. ' does not ship one. Rebuild the Switch template via build_release_template.bat.'
        end
        -- AuthoringTool needs the NSS via --nss, but everything in codeFolder
        -- is also passed to --program. Keep the verification input outside it.
        local verificationNss = pathJoin(tmpDir, 'main.nss')
        local moved, reason = os.rename(nssFile, verificationNss)
        if not moved then
            return 'Failed to stage the NSS outside the program code folder: ' .. tostring(reason)
        end
        if findNssFile(codeFolder) then
            return 'More than one NSS file in the Switch template code folder: ' .. codeFolder
        end
        nssFile = verificationNss
    end
    -- gather files into appFolder (tmp folder); exclude the whole build output
    -- root so a dstDir inside the project can never be re-copied into the app.
    ret = copyDir(args.srcDir, appFolder, { args.dstDir })
    if ret ~= 0 then
        return "Failed to copy " .. args.srcDir .. ' to ' .. appFolder
    end
    logd("Copied " .. args.srcDir .. ' to ' .. appFolder)
    -- compile .lua
    local rc = compileScriptsAndMakeCAR(args.nxsParams, appFolder, appFolder, tmpDir)
    if not rc then
        return "Failed to create .car file"
    end
    logd("Created .car file")
    -- delete .lua, .lu, etc
    deleteUnusedFiles(appFolder, getExcludePredecate())
    
    -- Verify .nrr folder still exists after cleanup
    local nrrCheckPath = pathJoin(appFolder, '.nrr')
    if isDir(nrrCheckPath) then
        log('Verified .nrr folder exists after cleanup: ' .. nrrCheckPath)
    else
        log('Note: .nrr folder will be created after NRO copy')
    end
    -- Copy NRO files from template root to appFolder
    local nroCount = 0
    local nroFiles = {}
    for file in lfs.dir(templateFolder) do
        if file ~= '.' and file ~= '..' and file:match("%.nro$") then
            local src = pathJoin(templateFolder, file)
            local dst = pathJoin(appFolder, file)
            if copyFile(src, dst) then
                log('Copied NRO to appFolder: ' .. file)
                nroCount = nroCount + 1
                nroFiles[#nroFiles + 1] = file
            else
                log('FAILED to copy NRO: ' .. file)
            end
        end
    end
    log('Total NRO files copied: ' .. nroCount)
    -- Copy NRS files from template root to appFolder (one .nrs per .nro is required for publishable builds)
    local nrsCount = 0
    local nrsFiles = {}
    for file in lfs.dir(templateFolder) do
        if file ~= '.' and file ~= '..' and file:match("%.nrs$") then
            local src = pathJoin(templateFolder, file)
            local dst = pathJoin(appFolder, file)
            if copyFile(src, dst) then
                log('Copied NRS to appFolder: ' .. file)
                nrsCount = nrsCount + 1
                nrsFiles[#nrsFiles + 1] = file
            else
                log('FAILED to copy NRS: ' .. file)
            end
        end
    end
    log('Total NRS files copied: ' .. nrsCount)
    -- Copy .nrr folder from template to appFolder
    -- Per docs: ".nrr directory must be under the data region directory"
    local nrrCount = 0
    local nrrFiles = {}
    local appNrrFolder = pathJoin(appFolder, '.nrr')
    
    local nrrSourceLocations = {
        pathJoin(templateFolder, '.nrr'),
        pathJoin(templateFolder, 'nrr'),
    }
    
    local foundNrrSource = nil
    for _, loc in ipairs(nrrSourceLocations) do
        if isDir(loc) then
            local count = countFilesWithExtension(loc, 'nrr')
            if count > 0 then
                foundNrrSource = loc
                log('Found NRR source folder: ' .. loc .. ' with ' .. count .. ' file(s)')
                break
            end
        end
    end
    
    if foundNrrSource then
        lfs.mkdir(appNrrFolder)
        log('Created .nrr folder: ' .. appNrrFolder)
        
        for file in lfs.dir(foundNrrSource) do
            if file ~= '.' and file ~= '..' and file:match("%.nrr$") then
                local src = pathJoin(foundNrrSource, file)
                local dst = pathJoin(appNrrFolder, file)
                if copyFile(src, dst) then
                    local size = lfs.attributes(dst, 'size') or 0
                    log('Copied NRR: ' .. file .. ' (' .. size .. ' bytes)')
                    nrrCount = nrrCount + 1
                    nrrFiles[#nrrFiles + 1] = file
                else
                    log('FAILED to copy NRR: ' .. file)
                end
            end
        end
    else
        log('WARNING: No .nrr folder found in template!')
    end
    
    log('Total NRR files copied: ' .. nrrCount)
    -- Build App
    local metafile = args.nmetaPath
    if not isFile(metafile) then
        return 'Missing ' .. metafile .. ' file'
    end
    log('Using metafile: ' .. metafile)
    local nspfile = pathJoin(nxsappFolder, args.applicationName .. '.nsp')
    local descfile = pathJoin(nxsRoot, 'Resources', 'SpecFiles', 'Application.desc')

    -- Update .npdm file. All SDK tool invocations use os.execute for the same
    -- reason as unzip/copyDir above: processExecute -> CommandLineRunner returns
    -- -1 for these command shapes, masking success and failure alike.
    -- os.execute -> C system() -> cmd /c accepts them just fine.
    local cmd = '"' .. nxsRoot .. '\\Tools\\CommandLineTools\\MakeMeta\\MakeMeta.exe'
    cmd = cmd .. ' --desc ' .. nxsRoot .. '\\Resources\\SpecFiles\\Application.desc'
    cmd = cmd .. ' --meta "' .. metafile .. '"'
    cmd = cmd .. ' -o "' .. pathJoin(codeFolder, 'main.npdm') .. '"'
    cmd = cmd .. '"'
    log('Creating NPDM file...')
    logd('MakeMeta command: ' .. cmd)
    local osResult = os.execute(cmd)
    local metaRc = (osResult == 0 or osResult == true) and 0 or 1
    log('MakeMeta retcode: ' .. tostring(metaRc))
    if metaRc ~= 0 then
        return 'MakeMeta failed (rc=' .. tostring(metaRc) .. ')'
    end

    if nssFile then
        log('Found NSS file: ' .. nssFile)
        local nssInfo = checkNssFile(nssFile)
        log('NSS file info: ' .. nssInfo)
    else
        log('No NSS file found in code folder')
    end
    local hasNroFiles = nroCount > 0
    local hasNrsFiles = nrsCount > 0
    log('Has NRO files: ' .. tostring(hasNroFiles) .. ' (' .. nroCount .. ')')
    log('Has NRS files: ' .. tostring(hasNrsFiles) .. ' (' .. nrsCount .. ')')
    log('Has NRR files: ' .. tostring(nrrCount > 0) .. ' (' .. nrrCount .. ')')

    -- Build AuthoringTool creatensp command
    -- -v emits [Info]/[Progress] lines so we can see how far the tool got when capturing output;
    -- --utf8 forces UTF-8 output so redirected logs don't get mangled.
    -- Wrap the entire command in outer `"..."`. cmd /c strips the outer pair
    -- when the very first and very last chars are both `"`, otherwise it uses
    -- the "old" strategy of stripping only the leading quote and the last
    -- inner quote -- which breaks apart the AuthoringTool arg list.
    cmd = '""' .. nxsRoot .. '\\Tools\\CommandLineTools\\AuthoringTool\\AuthoringTool.exe"'
    cmd = cmd .. ' -v --utf8 --platform NX'
    cmd = cmd .. ' creatensp'
    cmd = cmd .. ' -o "' .. nspfile .. '"'
    cmd = cmd .. ' --desc "' .. descfile .. '"'
    cmd = cmd .. ' --meta "' .. metafile .. '"'
    cmd = cmd .. ' --type Application'
    cmd = cmd .. ' --program "' .. codeFolder .. '" "' .. appFolder .. '"'

    if hasNroFiles then
        cmd = cmd .. ' --nro "' .. appFolder .. '"'
        log('Added --nro: ' .. appFolder)
    end

    if args.publishable == nil then
        return 'Missing publishable packaging argument'
    end
    local publishable = args.publishable and true or false
    log('Build mode: ' .. (publishable and 'publishable' or 'dev (non-publishable)'))

    if publishable then
        cmd = cmd .. ' --error-unpublishable'
        cmd = cmd .. ' --nss "' .. nssFile .. '"'
        log('Added --nss: ' .. nssFile)

        if hasNroFiles then
            if not hasNrsFiles then
                log('ERROR: publishable build needs one .nrs per .nro (have ' .. nroCount .. ' NRO, 0 NRS)')
                log('ERROR: rebuild the Switch template (build_release_template.bat) so .nrs files are packaged alongside the .nro files')
                return 'Publishable build requires .nrs file for each .nro, but none were found in template'
            end
            if nrsCount ~= nroCount then
                log('WARNING: nrsCount (' .. nrsCount .. ') does not match nroCount (' .. nroCount .. ') -- AuthoringTool may reject the build')
            end
            -- 22.2.0 syntax: single --nrs switch followed by all paths separated by spaces.
            local nrsArg = ''
            for _, file in ipairs(nrsFiles) do
                nrsArg = nrsArg .. ' "' .. pathJoin(appFolder, file) .. '"'
            end
            cmd = cmd .. ' --nrs' .. nrsArg
            log('Added --nrs (' .. nrsCount .. ' file(s))')
        end
    else
        cmd = cmd .. ' --ignore-nss-nrs-option'
        log('Added --ignore-nss-nrs-option (dev build, NSP will not be publishable)')
    end
    cmd = cmd .. '"'  -- close the outer wrapping quote (see comment above)

    log('Building App...')
    logd('Command: ' .. cmd)
    osResult = os.execute(cmd)
    local execSuccess = (osResult == 0) or (osResult == true)
    local authRc = execSuccess and 0 or 1

    -- A submission error is fatal even when AuthoringTool leaves an NSP on disk.
    if publishable and not execSuccess then
        return 'AuthoringTool creatensp failed for the Switch submission (rc=' .. tostring(osResult) .. ')'
    end

    -- Also check the output: AuthoringTool can exit successfully without leaving a valid NSP.
    local nspSize = (isFile(nspfile) and (lfs.attributes(nspfile, 'size') or 0)) or 0
    local nspLooksValid = false
    if nspSize > 0 then
        local nspf = io.open(nspfile, 'rb')
        if nspf then
            local magic = nspf:read(4) or ''
            nspf:close()
            nspLooksValid = (magic == 'PFS0')
        end
    end

    if not isFile(nspfile) then
        log('ERROR: AuthoringTool did not produce an NSP (rc=' .. tostring(authRc) .. ')')
        return 'Failed to build NX Switch App'
    elseif not nspLooksValid then
        log('ERROR: NSP at ' .. nspfile .. ' is empty or missing PFS0 magic bytes (rc=' .. tostring(authRc) .. ', size=' .. nspSize .. ')')
        return 'Failed to build NX Switch App'
    elseif not execSuccess then
        log('NOTE: AuthoringTool exited rc=' .. tostring(authRc) .. ' but the NSP looks complete (PFS0, ' .. nspSize .. ' bytes); treating as success')
    end

    log('Build succeeded: ' .. nspfile .. ' (' .. nspSize .. ' bytes)')
    if not publishable then
        log('NOTE: NSP is unpublishable (--ignore-nss-nrs-option). Set publishable=true in the packaging args for a publishable build.')
    end
    removeDir(tmpDir)
    return nil
end
