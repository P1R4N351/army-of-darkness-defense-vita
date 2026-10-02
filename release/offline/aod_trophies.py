"""Strict own-APK-only achievement dispatch patch, AFTER LUA_CORE, BEFORE resign.

No predicates/counters changed and no retrospective counter interpretation.
The game's Lua os.getenv binding calls the loader's reserved native bridge.
Ordinary getenv still returns nil. print is NOT used.
"""
import hashlib

REL = 'scripts.archondb/achievements-3k7cwojlwz3adpq2a5wlgkff4e4vkvis.lua'
CORE_SHA256 = '721c22a7b349216affaf9e09032b6b9d4268d66a659b1b9068faee658d81629f'
PATCHED_SHA256 = '26c8a3c279dd5e605395c1fddedbea8415630f043e19dbedb16f90d2e6b0cfb5'
OLD = '''local function pushAchievementData(achievement_id, progress)
    progress = math.min(100.0, math.max(0.0, progress))
    local gc = nil -- [aod-offline] no GameCenter reporting; local progress counters unchanged
    if gc then
        gc:reportCompletion(progress, achievement_id, function() end)
    end
end'''
NEW = '''local function pushAchievementData(achievement_id, progress)
    -- [aod-local-trophies-v1] Only the original completed predicates emit events.
    -- Reject partial, NaN, infinity, bad types and out-of-range inputs BEFORE clamping.
    if type(progress) ~= "number" or progress ~= 100.0 then return end
    if type(achievement_id) ~= "string" then return end
    -- os is opened by the game (also used for time/clock). getenv resolves to
    -- the loader, consumes only this namespace, and never contacts a service.
    if os and type(os.getenv) == "function" then
        pcall(os.getenv, "AOD_LOCAL_TROPHY_V1:" .. achievement_id)
    end
end'''


def transform(data):
    digest = hashlib.sha256(data).hexdigest()
    if digest == PATCHED_SHA256:
        return data
    if digest != CORE_SHA256:
        raise ValueError('trophy patch refuses unknown Lua/core version: ' + digest)
    old = OLD.encode('ascii')
    if data.count(old) != 1:
        raise ValueError('trophy dispatch anchor is not unique')
    result = data.replace(old, NEW.encode('ascii'))
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
        raise ValueError('trophy patch output hash mismatch')
    return result


def apply(tree):
    before = tree.read(REL)
    after = transform(before)
    if after != before:
        tree.write(REL, after, 'local homebrew completion dispatch; original predicates unchanged')
