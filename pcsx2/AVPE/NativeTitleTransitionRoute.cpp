// AVP:E title-transition diagnostic HTTP route. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeTitleTransitionRoute.h"

#include "AVPE/NativeTitleTransition.h"

namespace AVPE::NativeTitleTransitionRoute
{
	lucent::http::Response Start()
	{
		if (!NativeTitleTransition::Start())
		{
			return lucent::http::Response::json(409, "Conflict",
				R"({"error":"title transition requires the supported AVP:E control-test target"})");
		}
		return lucent::http::Response::json(200, "OK", NativeTitleTransition::SnapshotJson());
	}

	lucent::http::Response Snapshot()
	{
		return lucent::http::Response::json(200, "OK", NativeTitleTransition::SnapshotJson());
	}
} // namespace AVPE::NativeTitleTransitionRoute
