#include "actor/MiEmptyActor.h"
#include "core/MiTypeRegistry.h"

namespace MiEngine {

MiEmptyActor::MiEmptyActor()
    : MiActor()
{
    setName("EmptyActor");
}

// Register the type
MI_REGISTER_TYPE(MiEmptyActor)

} // namespace MiEngine
