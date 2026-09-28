

--| [aod-offline] LOCAL OFFLINE PURCHASES |--
-- Personal offline Vita port: Google billing is never contacted (the native bridge fails closed).
-- A coin-store selection is approved locally on the device and fulfilled exactly once with the
-- product's normal amount from globals.purchases. There is no receipt, no signature and no account:
-- every local grant is recorded in the save (UserData) under offline_iap_* keys.
--   exactly-once: grant + ledger entry are written synchronously in one call; the UI callback is
--                 deferred one action tick and guarded so a repeated/late tick cannot re-grant or
--                 re-fire; a second request while one is pending is refused.
--   consumables : gold tiers stack like the store did; purchased gold is also added to GoldFromIAP,
--                 which the game's own "reset data" keeps (optionsmenuwindow reset handler).
-- Nothing runs at load time; grants happen only from an explicit coin-store button press.

OFFLINE_SEQ_KEY = "offline_iap_seq"
OFFLINE_TXN_PREFIX = "offline_iap_txn_"

local offline_pending = false
local offline_pending_since = 0
local OFFLINE_STALE_SECONDS = 5   -- release a pending flag whose UI tick never ran (grant already applied)

local function offlineAmount(product_id)
    for _, p in ipairs(chunks.globals.purchases or {}) do
        if p.id == product_id then
            return p.gold
        end
    end
    return nil
end

local function offlineDefer(owner, fn)
    if owner ~= nil then
        owner:addAction(LuaAction(0.1, function(inner) fn() end))
    else
        fn()
    end
end

function offlinePurchasePending()
    if offline_pending and os.time() - offline_pending_since >= OFFLINE_STALE_SECONDS then
        offline_pending = false
    end
    return offline_pending
end

function offlinePurchase(product_id, owner, success_callback, failure_callback)
    if offlinePurchasePending() then
        print("offline IAP: request ignored, another purchase is pending")
        return false
    end

    local amount = offlineAmount(product_id)
    if amount == nil or amount <= 0 then
        print("offline IAP: unknown product " .. tostring(product_id))
        local failed = false
        offlineDefer(owner, function()
            if failed then
                return
            end
            failed = true
            if failure_callback then
                failure_callback()
            end
        end)
        return false
    end

    offline_pending = true
    offline_pending_since = os.time()
    local ud = UserData.instance()
    local raw_seq = ud:getValue(OFFLINE_SEQ_KEY)
    local seq = (tonumber(raw_seq) or 0) + 1

    ud:setGold(ud:getGold() + amount)
    ud:setGoldFromIAP(ud:getGoldFromIAP() + amount)
    ud:setValue(OFFLINE_SEQ_KEY, tostring(seq))
    ud:setValue(OFFLINE_TXN_PREFIX .. tostring(seq), product_id .. ":" .. tostring(amount) .. ":local")
    print("offline IAP: granted " .. tostring(amount) .. " gold for " .. product_id .. " (txn " .. tostring(seq) .. ")")

    local delivered = false
    offlineDefer(owner, function()
        if delivered then
            return
        end
        delivered = true
        offline_pending = false
        if success_callback then
            success_callback()
        end
    end)
    return true
end
