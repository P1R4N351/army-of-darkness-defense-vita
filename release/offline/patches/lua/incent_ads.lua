--| module |--
-- [aod-offline] Incentivized video ads removed. Same public API as the shipped module;
-- no provider is contacted, no ad unit identifiers are kept, and every request resolves
-- synchronously as "unavailable" so callers never wait. No reward is ever granted here.
module(...,package.seeall)

show_handle = nil
preload_handle = {}
ad_ready_callback = nil

function maintainPreloadCycle() end

function videoAvailable(tag)
    return false
end

function preloadAd(tag, on_load_callback, on_error_callback)
    if on_error_callback then
        on_error_callback()
    end
end

function showIncentVideo(tag, success_callback, fail_callback)
    if fail_callback then
        fail_callback()
    end
    return nil
end

function getGoogleAdUnit()
    return ""
end
