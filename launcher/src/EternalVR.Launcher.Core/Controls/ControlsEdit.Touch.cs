using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Controls
{
    /// <summary>
    /// The touch sensors of a control map in the editor (the layer's touch_inputs.hpp): which hands' rests of the thumb-rest
    /// wheel a map binds like buttons, the thumb rest or, with Face buttons as thumb rest, A/B or X/Y touched.
    /// </summary>
    public sealed partial class ControlsEdit
    {
        private const string FrameProfile = "/interaction_profiles/valve/frame_controller_valve";

        /// <summary>
        /// The hands whose rest of the thumb-rest wheel the map for <paramref name="hand"/> binds to an action, in any press
        /// kind: the thumb rest, or with <paramref name="faceTouch"/> on a controller without a thumb rest, the primary or
        /// secondary button's touch (the layer's bindsWheelRest, input_mapper.hpp).
        /// </summary>
        public IReadOnlyList<ControlHand> BoundRests(Handedness hand, bool faceTouch) =>
            new[] { ControlHand.Left, ControlHand.Right }
                .Where(h => RestInputs(h, faceTouch).Any(input => Binds(hand, h, input)))
                .ToList();

        /// <summary>True when the map for <paramref name="hand"/> binds <paramref name="input"/> of <paramref name="h"/> to an action.</summary>
        public bool Binds(Handedness hand, ControlHand h, ButtonInput input) =>
            Enum.GetValues(typeof(PressKind)).Cast<PressKind>().Any(p =>
            {
                var action = Get(hand, BindingKey.Button(h, input, p));
                return action != null && action != ControlNames.Unbound;
            });

        private IEnumerable<ButtonInput> RestInputs(ControlHand h, bool faceTouch)
        {
            yield return ButtonInput.ThumbRest;
            if (FaceTouchIsRest(h, faceTouch))
            {
                yield return ButtonInput.PrimaryTouch;
                yield return ButtonInput.SecondaryTouch;
            }
        }

        /// <summary>
        /// Whether the wheel reads a face button's touch as the rest of <paramref name="h"/>: Face buttons as thumb rest on, no
        /// thumb rest there, a face button with a touch sensor, and not the Steam Frame, whose bumper holds the wheel (the
        /// layer's rest_touch_bindings.hpp).
        /// </summary>
        private bool FaceTouchIsRest(ControlHand h, bool faceTouch) =>
            faceTouch && !HasInput(h, ButtonInput.ThumbRest) && Family.ProfilePath != FrameProfile
            && (HasInput(h, ButtonInput.PrimaryTouch) || HasInput(h, ButtonInput.SecondaryTouch));

        /// <summary>Whether <paramref name="h"/> has a rest the wheel reads.</summary>
        private bool WheelRest(ControlHand h, bool faceTouch) => HasInput(h, ButtonInput.ThumbRest) || FaceTouchIsRest(h, faceTouch);

        private string RestName(ControlHand h, bool faceTouch) => FaceTouchIsRest(h, faceTouch) ? "face-button touch" : "thumb rest";
    }
}
