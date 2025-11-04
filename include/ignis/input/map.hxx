#ifndef IGNIS_INPUT_MAP_HXX
#define IGNIS_INPUT_MAP_HXX

#include <execution>
#include <ranges>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/container/small_vector.hpp>

#include <ignis/input/event.hxx>
#include <ignis/input/axis.hxx>
#include <ignis/graphics/window.hxx>

namespace Ignis::Input
{
    class Map final
    {
        struct ButtonAxisData
        {
            double value;
            ButtonEvent positive;
            std::optional <ButtonEvent> negative;
        };

        struct MouseAxisData
        {
            double value;
            double multiplier;
            MouseBinding binding;
        };

        void reserve_for_button_axis (ButtonEvent const positiveEvent,
            std::optional <ButtonEvent> const negativeEvent)
        {
            myButtonAxesData.reserve (myButtonAxesData.size() + 1);

            bool const reserveForPositive = !myButtonEventAxes.contains (positiveEvent);
            bool const reserveForNegative = negativeEvent.has_value() && !myButtonEventAxes.contains (*negativeEvent);

            size_t const eventAxesToReserve = (reserveForPositive ? 1 : 0) + (reserveForNegative ? 1 : 0);

            myButtonEventAxes.reserve (myButtonEventAxes.size() + eventAxesToReserve);

            auto& positiveEventAxes = myButtonEventAxes [positiveEvent];
            positiveEventAxes.reserve (positiveEventAxes.size() + 1);

            if (reserveForNegative) {
                auto& negativeEventAxes = myButtonEventAxes [*negativeEvent];
                negativeEventAxes.reserve (negativeEventAxes.size() + 1);
            }
        }

    public:
        Map() noexcept :
            myButtonAxisVersioneer (0),
            myMouseAxisVersioneer (0),
            myMouseState (0, 0, 0, 0)
        {}

        [[nodiscard]] ButtonAxis make_axis
        (ButtonEvent const positiveEvent, std::optional <ButtonEvent> const negativeEvent = std::nullopt)
        {
            assert (!negativeEvent.has_value() || positiveEvent != *negativeEvent);

            reserve_for_button_axis (positiveEvent, negativeEvent);

            ButtonAxis const axis { myButtonAxisVersioneer++ };

            myButtonAxesData.emplace (std::piecewise_construct,
                std::forward_as_tuple (axis),
                std::forward_as_tuple (0, positiveEvent, negativeEvent));

            myButtonEventAxes [positiveEvent].emplace_back (axis);

            if (negativeEvent.has_value())
                myButtonEventAxes [*negativeEvent].emplace_back (axis);

            return axis;
        }

        [[nodiscard]] MouseAxis make
        (MouseBinding const binding, double const multiplier = 1)
        {
            MouseAxis const axis { myMouseAxisVersioneer++ };

            myMouseAxesData.emplace (std::piecewise_construct,
                std::forward_as_tuple (axis),
                std::forward_as_tuple (0, multiplier, binding));

            return axis;
        }

        void set_value (ButtonAxis const axis, double const value) {
            myButtonAxesData.at(axis).value = value;
        }

        void set_value (MouseAxis const axis, double const value) {
            myMouseAxesData.at(axis).value = value;
        }

        [[nodiscard]] double get_value (ButtonAxis const axis) const {
            return myButtonAxesData.at(axis).value;
        }

        [[nodiscard]] double get_value (MouseAxis const axis) const {
            return myMouseAxesData.at(axis).value;
        }

        void erase_axis (ButtonAxis const axis) noexcept
        {
            if (auto const iter = myButtonAxesData.find(axis);
                iter != myButtonAxesData.end()) [[likely]]
            {
                [[maybe_unused]] auto const [value, positive, negative] = iter->second;

                auto eraseAxis = [axis] (auto& cont) {
                    auto const [toEraseBegin, toEraseEnd] = std::ranges::remove (cont, axis);
                    cont.erase (toEraseBegin, toEraseEnd);
                };

                eraseAxis (myButtonEventAxes.at(positive));

                if (negative.has_value())
                    eraseAxis (myButtonEventAxes.at(*negative));

                myButtonAxesData.erase (iter);
            }
        }

        void erase_axis (MouseAxis const axis) noexcept {
            myMouseAxesData.erase(axis);
        }

        void extract_events (Graphics::Window& window) noexcept
        {
            window.swap_events_buffers (myEvents);
            myMouseState = window.get_mouse_state ();
        }

        void apply_events ()
        {
            std::ranges::for_each (myEvents,
            [&] (ButtonEvent const event)
            {
                if (const auto iter = myButtonEventAxes.find(event);
                    iter != myButtonEventAxes.end())
                {
                    auto const& axes = iter->second;

                    for (auto const axis : axes)
                    {
                        auto& [value, positive, negative] = myButtonAxesData.at(axis);

                        if (positive == event)
                            value = std::min(value + 1.0, 1.0);

                        if (negative == event)
                            value = std::max(value - 1.0, -1.0);
                    }
                }
            });

            for (auto& [value, multiplier, binding] : myMouseAxesData | std::views::values)
            {
                switch (binding)
                {
                case MouseBinding::cursor_x:
                    value = myMouseState.cursorX * multiplier;
                    break;

                case MouseBinding::cursor_y:
                    value = myMouseState.cursorY * multiplier;
                    break;

                case MouseBinding::scroll_x:
                    value = myMouseState.scrollX * multiplier;
                    break;

                case MouseBinding::scroll_y:
                    value = myMouseState.scrollY * multiplier;
                    break;
                }
            }

            myEvents.clear();
        }

    private:
        boost::unordered_flat_map
            <ButtonAxis, ButtonAxisData, Detail::AxisHash>
        myButtonAxesData;

        boost::unordered_flat_map
            <ButtonEvent, boost::container::small_vector <ButtonAxis, 10>, Detail::EventHash>
        myButtonEventAxes;

        boost::unordered_flat_map
            <MouseAxis, MouseAxisData, Detail::AxisHash>
        myMouseAxesData;

        size_t myButtonAxisVersioneer;
        size_t myMouseAxisVersioneer;

        std::vector <ButtonEvent> myEvents;
        MouseState myMouseState;
    };
}

#endif