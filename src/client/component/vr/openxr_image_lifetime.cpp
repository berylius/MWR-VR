#include <std_include.hpp>
#include "openxr_d3d11.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	bool wait_acquired_image(const dispatch_table& dispatch, eye_swapchain& eye,
	                        XrResult& last_result) noexcept
	{
		if (!eye.acquired || eye.handle == XR_NULL_HANDLE)
		{
			last_result = XR_ERROR_CALL_ORDER_INVALID;
			return false;
		}
		if (eye.waited) { last_result = XR_SUCCESS; return true; }
		const XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
		last_result = dispatch.wait_swapchain_image(eye.handle, &wait);
		if (XR_FAILED(last_result) || last_result == XR_TIMEOUT_EXPIRED) return false;
		eye.waited = true;
		return true;
	}

	bool release_acquired_image(const dispatch_table& dispatch,
	                            eye_swapchain& eye,
	                            XrResult& last_result) noexcept
	{
		if (!eye.acquired || !eye.waited || eye.handle == XR_NULL_HANDLE)
			return false;
		const XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		last_result = dispatch.release_swapchain_image(eye.handle, &release_info);
		if (XR_FAILED(last_result))
			return false;
		eye.acquired = false;
		eye.waited = false;
		return true;
	}

	bool destroy_eye_swapchain(const dispatch_table& dispatch,
	                           eye_swapchain& eye,
	                           XrResult& last_result,
	                           const bool destroy_handle) noexcept
	{
		if (!destroy_handle)
			return false;
		if (eye.handle != XR_NULL_HANDLE)
		{
			if (eye.acquired && (!wait_acquired_image(dispatch, eye, last_result) ||
				!release_acquired_image(dispatch, eye, last_result))) return false;
			if (dispatch.destroy_swapchain == nullptr)
			{
				last_result = XR_ERROR_FUNCTION_UNSUPPORTED;
				return false;
			}
			eye.render_targets.clear();
			eye.images.clear();
			eye.wrapped.clear();
			last_result = dispatch.destroy_swapchain(eye.handle);
			if (XR_FAILED(last_result))
				return false;
		}
		eye = {};
		return true;
	}
}
#endif
