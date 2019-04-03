//
// Aspia Project
// Copyright (C) 2019 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "desktop/win/dxgi_duplicator_controller.h"

#include "base/logging.h"
#include "desktop/desktop_frame_simple.h"
#include "desktop/win/dxgi_frame.h"
#include "desktop/win/screen_capture_utils.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include <windows.h>

namespace desktop {

// static
std::string DxgiDuplicatorController::resultName(DxgiDuplicatorController::Result result)
{
    switch (result)
    {
        case Result::SUCCEEDED:
            return "Succeeded";

        case Result::UNSUPPORTED_SESSION:
            return "Unsupported session";

        case Result::FRAME_PREPARE_FAILED:
            return "Frame preparation failed";

        case Result::INITIALIZATION_FAILED:
            return "Initialization failed";

        case Result::DUPLICATION_FAILED:
            return "Duplication failed";

        case Result::INVALID_MONITOR_ID:
            return "Invalid monitor id";

        default:
            return "Unknown error";
    }
}

// static
base::scoped_refptr<DxgiDuplicatorController> DxgiDuplicatorController::instance()
{
    // The static instance won't be deleted to ensure it can be used by other
    // threads even during program exiting.
    static DxgiDuplicatorController* instance = new DxgiDuplicatorController();

    return base::scoped_refptr<DxgiDuplicatorController>(instance);
}

// static
bool DxgiDuplicatorController::isCurrentSessionSupported()
{
    DWORD session_id = 0;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        LOG(LS_WARNING) << "Failed to retrieve current session Id, current binary "
                           "may not have required priviledge";
        return false;
    }

    return session_id != 0;
}

DxgiDuplicatorController::DxgiDuplicatorController()
    : refcount_(0)
{
    // Nothing
}

void DxgiDuplicatorController::addRef()
{
    int refcount = (++refcount_);
    DCHECK(refcount > 0);
}

void DxgiDuplicatorController::release()
{
    int refcount = (--refcount_);
    DCHECK(refcount >= 0);

    if (refcount == 0)
    {
        LOG(LS_WARNING) << "Count of references reaches zero, "
                           "DxgiDuplicatorController will be unloaded.";
        unload();
    }
}

bool DxgiDuplicatorController::isSupported()
{
    std::scoped_lock lock(lock_);
    return initialize();
}

bool DxgiDuplicatorController::retrieveD3dInfo(D3dInfo* info)
{
    bool result = false;
    {
        std::scoped_lock lock(lock_);
        result = initialize();
        *info = d3d_info_;
    }

    if (!result)
    {
        LOG(LS_WARNING) << "Failed to initialize DXGI components, the D3dInfo "
                           "retrieved may not accurate or out of date.";
    }

    return result;
}

DxgiDuplicatorController::Result DxgiDuplicatorController::duplicate(DxgiFrame* frame)
{
    return doDuplicate(frame, -1);
}

DxgiDuplicatorController::Result DxgiDuplicatorController::duplicateMonitor(
    DxgiFrame* frame, int monitor_id)
{
    DCHECK_GE(monitor_id, 0);
    return doDuplicate(frame, monitor_id);
}

QPoint DxgiDuplicatorController::dpi()
{
    std::scoped_lock lock(lock_);
    if (initialize())
        return dpi_;

    return QPoint();
}

int DxgiDuplicatorController::screenCount()
{
    std::scoped_lock lock(lock_);

    if (initialize())
        return screenCountUnlocked();

    return 0;
}

bool DxgiDuplicatorController::deviceNames(std::vector<std::string>* output)
{
    std::scoped_lock lock(lock_);

    if (initialize())
    {
        deviceNamesUnlocked(output);
        return true;
    }

    return false;
}

DxgiDuplicatorController::Result DxgiDuplicatorController::doDuplicate(
    DxgiFrame* frame, int monitor_id)
{
    DCHECK(frame);

    std::scoped_lock lock(lock_);

    // The dxgi components and APIs do not update the screen resolution without
    // a reinitialization. So we use the GetDC() function to retrieve the screen
    // resolution to decide whether dxgi components need to be reinitialized.
    // If the screen resolution changed, it's very likely the next Duplicate()
    // function call will fail because of a missing monitor or the frame size is
    // not enough to store the output. So we reinitialize dxgi components in-place
    // to avoid a capture failure.
    // But there is no guarantee GetDC() function returns the same resolution as
    // dxgi APIs, we still rely on dxgi components to return the output frame
    // size.
    // TODO(zijiehe): Confirm whether IDXGIOutput::GetDesc() and
    // IDXGIOutputDuplication::GetDesc() can detect the resolution change without
    // reinitialization.
    if (display_configuration_monitor_.isChanged())
    {
        deinitialize();
    }

    if (!initialize())
    {
        if (succeeded_duplications_ == 0 && !isCurrentSessionSupported())
        {
            LOG(LS_WARNING) << "Current binary is running in session 0. DXGI "
                               "components cannot be initialized.";
            return Result::UNSUPPORTED_SESSION;
        }

        // Cannot initialize COM components now, display mode may be changing.
        return Result::INITIALIZATION_FAILED;
    }

    if (!frame->prepare(selectedDesktopSize(monitor_id), monitor_id))
        return Result::FRAME_PREPARE_FAILED;

    *frame->frame()->updatedRegion() = QRegion();

    if (doDuplicateUnlocked(frame->context(), monitor_id, frame->frame()))
    {
        ++succeeded_duplications_;
        return Result::SUCCEEDED;
    }
    if (monitor_id >= screenCountUnlocked())
    {
        // It's a user error to provide a |monitor_id| larger than screen count. We
        // do not need to deinitialize.
        return Result::INVALID_MONITOR_ID;
    }

    // If the |monitor_id| is valid, but doDuplicateUnlocked() failed, something must be wrong from
    // capturer APIs. We should deinitialize().
    deinitialize();
    return Result::DUPLICATION_FAILED;
}

void DxgiDuplicatorController::unload()
{
    std::scoped_lock lock(lock_);
    deinitialize();
}

void DxgiDuplicatorController::unregister(const Context* const context)
{
    std::scoped_lock lock(lock_);

    if (contextExpired(context))
    {
        // The Context has not been setup after a recent initialization, so it
        // should not been registered in duplicators.
        return;
    }

    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        duplicators_[i].unregister(&context->contexts[i]);
    }
}

bool DxgiDuplicatorController::initialize()
{
    if (!duplicators_.empty())
        return true;

    if (doInitialize())
        return true;

    deinitialize();
    return false;
}

bool DxgiDuplicatorController::doInitialize()
{
    DCHECK(desktop_rect_.isEmpty());
    DCHECK(duplicators_.empty());

    d3d_info_.min_feature_level = static_cast<D3D_FEATURE_LEVEL>(0);
    d3d_info_.max_feature_level = static_cast<D3D_FEATURE_LEVEL>(0);

    std::vector<D3dDevice> devices = D3dDevice::enumDevices();
    if (devices.empty())
    {
        LOG(LS_WARNING) << "No D3dDevice found";
        return false;
    }

    for (size_t i = 0; i < devices.size(); i++)
    {
        D3D_FEATURE_LEVEL feature_level = devices[i].d3dDevice()->GetFeatureLevel();

        if (d3d_info_.max_feature_level == 0 || feature_level > d3d_info_.max_feature_level)
            d3d_info_.max_feature_level = feature_level;

        if (d3d_info_.min_feature_level == 0 || feature_level < d3d_info_.min_feature_level)
            d3d_info_.min_feature_level = feature_level;

        DxgiAdapterDuplicator duplicator(devices[i]);
        // There may be several video cards on the system, some of them may not
        // support IDXGOutputDuplication. But they should not impact others from
        // taking effect, so we should continually try other adapters. This usually
        // happens when a non-official virtual adapter is installed on the system.
        if (!duplicator.initialize())
        {
            LOG(LS_WARNING) << "Failed to initialize DxgiAdapterDuplicator on adapter " << i;
            continue;
        }

        DCHECK(!duplicator.desktopRect().isEmpty());
        duplicators_.push_back(std::move(duplicator));

        desktop_rect_ = desktop_rect_.united(duplicators_.back().desktopRect());
    }

    translateRect();

    HDC hdc = GetDC(nullptr);
    // Use old DPI value if failed.
    if (hdc)
    {
        dpi_ = QPoint(GetDeviceCaps(hdc, LOGPIXELSX), GetDeviceCaps(hdc, LOGPIXELSY));
        ReleaseDC(nullptr, hdc);
    }

    ++identity_;

    if (duplicators_.empty())
    {
        LOG(LS_WARNING) << "Cannot initialize any DxgiAdapterDuplicator instance.";
    }

    return !duplicators_.empty();
}

void DxgiDuplicatorController::deinitialize()
{
    desktop_rect_ = QRect();
    duplicators_.clear();
    display_configuration_monitor_.reset();
}

bool DxgiDuplicatorController::contextExpired(const Context* const context) const
{
    DCHECK(context);
    return context->controller_id != identity_ ||
        context->contexts.size() != duplicators_.size();
}

void DxgiDuplicatorController::setup(Context* context)
{
    if (contextExpired(context))
    {
        DCHECK(context);

        context->contexts.clear();
        context->contexts.resize(duplicators_.size());

        for (size_t i = 0; i < duplicators_.size(); ++i)
            duplicators_[i].setup(&context->contexts[i]);

        context->controller_id = identity_;
    }
}

bool DxgiDuplicatorController::doDuplicateUnlocked(Context* context,
                                                   int monitor_id,
                                                   SharedFrame* target)
{
    setup(context);

    if (!ensureFrameCaptured(context, target))
        return false;

    bool result = false;
    if (monitor_id < 0)
    {
        // Capture entire screen.
        result = doDuplicateAll(context, target);
    }
    else
    {
        result = doDuplicateOne(context, monitor_id, target);
    }

    if (result)
    {
        //target->set_dpi(dpi_);
        return true;
    }

    return false;
}

bool DxgiDuplicatorController::doDuplicateAll(Context* context, SharedFrame* target)
{
    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        if (!duplicators_[i].duplicate(&context->contexts[i], target))
            return false;
    }

    return true;
}

bool DxgiDuplicatorController::doDuplicateOne(Context* context, int monitor_id, SharedFrame* target)
{
    DCHECK(monitor_id >= 0);

    for (size_t i = 0; i < duplicators_.size() && i < context->contexts.size(); ++i)
    {
        if (monitor_id >= duplicators_[i].screenCount())
        {
            monitor_id -= duplicators_[i].screenCount();
        }
        else
        {
            if (duplicators_[i].duplicateMonitor(&context->contexts[i],
                                                 monitor_id,
                                                 target))
            {
                target->setTopLeft(duplicators_[i].screenRect(monitor_id).topLeft());
                return true;
            }

            return false;
        }
    }
    return false;
}

int64_t DxgiDuplicatorController::numFramesCaptured() const
{
    int64_t min = std::numeric_limits<int64_t>::max();

    for (const auto& duplicator : duplicators_)
        min = std::min(min, duplicator.numFramesCaptured());

    return min;
}

QSize DxgiDuplicatorController::desktopSize() const
{
    return desktop_rect_.size();
}

QRect DxgiDuplicatorController::screenRect(int id) const
{
    DCHECK(id >= 0);

    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        if (id >= duplicators_[i].screenCount())
            id -= duplicators_[i].screenCount();
        else
            return duplicators_[i].screenRect(id);
    }

    return QRect();
}

int DxgiDuplicatorController::screenCountUnlocked() const
{
    int result = 0;

    for (auto& duplicator : duplicators_)
        result += duplicator.screenCount();

    return result;
}

void DxgiDuplicatorController::deviceNamesUnlocked(std::vector<std::string>* output) const
{
    DCHECK(output);

    for (auto& duplicator : duplicators_)
    {
        for (int i = 0; i < duplicator.screenCount(); ++i)
            output->push_back(duplicator.deviceName(i));
    }
}

QSize DxgiDuplicatorController::selectedDesktopSize(int monitor_id) const
{
    if (monitor_id < 0)
        return desktopSize();

    return screenRect(monitor_id).size();
}

bool DxgiDuplicatorController::ensureFrameCaptured(Context* context, SharedFrame* target)
{
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Milliseconds = std::chrono::milliseconds;

    // On a modern system, the FPS / monitor refresh rate is usually larger than
    // or equal to 60. So 17 milliseconds is enough to capture at least one frame.
    const Milliseconds ms_per_frame(17);

    // Skips the first frame to ensure a full frame refresh has happened before
    // this function returns.
    const int64_t frames_to_skip = 1;

    // The total time out milliseconds for this function. If we cannot get enough
    // frames during this time interval, this function returns false, and cause
    // the DXGI components to be reinitialized. This usually should not happen
    // unless the system is switching display mode when this function is being
    // called. 500 milliseconds should be enough for ~30 frames.
    const Milliseconds timeout_ms(500);

    if (numFramesCaptured() >= frames_to_skip)
        return true;

    std::unique_ptr<SharedFrame> fallback_frame;
    SharedFrame* shared_frame = nullptr;

    if (target->size().width() >= desktopSize().width() &&
        target->size().height() >= desktopSize().height())
    {
        // |target| is large enough to cover entire screen, we do not need to use |fallback_frame|.
        shared_frame = target;
    }
    else
    {
        fallback_frame = SharedFrame::wrap(
            FrameSimple::create(desktopSize(), PixelFormat::ARGB()));
        shared_frame = fallback_frame.get();
    }

    const TimePoint start_ms = Clock::now();
    TimePoint last_frame_start_ms;

    while (numFramesCaptured() < frames_to_skip)
    {
        if (numFramesCaptured() > 0)
        {
            // Sleep |ms_per_frame| before capturing next frame to ensure the screen
            // has been updated by the video adapter.
            std::this_thread::sleep_for(ms_per_frame - (Clock::now() - last_frame_start_ms));
        }

        last_frame_start_ms = Clock::now();

        if (!doDuplicateAll(context, shared_frame))
            return false;

        if (Clock::now() - start_ms > timeout_ms)
        {
            LOG(LS_ERROR) << "Failed to capture " << frames_to_skip << " frames within "
                          << timeout_ms.count() << " milliseconds.";
            return false;
        }
    }

    return true;
}

void DxgiDuplicatorController::translateRect()
{
    const QPoint position = QPoint(0, 0) - desktop_rect_.topLeft();

    desktop_rect_.translate(position);

    for (auto& duplicator : duplicators_)
        duplicator.translateRect(position);
}

} // namespace desktop