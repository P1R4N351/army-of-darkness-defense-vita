--| module |--
-- [aod-offline] Telemetry removed. Same public API as the shipped analytics module
-- (flush, send*); every call is a local no-op, so no AnalyticsEvent objects are built or sent.
module(...,package.seeall)

function flush() end
function sendLeaderboardUIOpened() end
function sendAchievementsUIOpened() end
function sendGameDataReset() end
function sendWaveEnd(wave_id, data) end
function sendIncentMenuDecision(did_watch, wave_id) end
function sendUnitSpawn(unit_type) end
function sendSpellCast(spell_type) end
