#ifndef IGNIS_INPUT_AXIS_HXX
#define IGNIS_INPUT_AXIS_HXX

#include <functional>

namespace Ignis::Detail
{
    class AxisHash;
}

namespace Ignis::Input
{
    class Map;

    enum class AxisType
        { button, mouse };

    template <AxisType Type>
    class Axis final
    {
        explicit Axis (size_t const handle) noexcept :
            myHandle (handle)
        {}

    public:
        [[nodiscard]] bool operator ==(Axis const&) const noexcept = default;
        [[nodiscard]] bool operator !=(Axis const&) const noexcept = default;

    private:
        size_t myHandle;

        friend class Map;
        friend class Detail::AxisHash;
    };

    using ButtonAxis = Axis <AxisType::button>;
    using MouseAxis = Axis <AxisType::mouse>;
}

namespace Ignis::Detail
{
    class AxisHash final
    {
    public:
        template <Input::AxisType Type>
        [[nodiscard]] size_t operator() (Input::Axis <Type> const axis)
        const noexcept {
            return std::hash <size_t> {} (axis.myHandle);
        }
    };
}

#endif