# Audio FX UI asset kit

Reference canvas: **2048 × 2048 px (1:1)**.

The six effect panels use a **512 × 670 px** design box (ratio **0.764:1**) in a 3 × 2 grid. Recommended gap: **20 px**. The Master FX panel is **1600 × 400 px (4:1)**.

Large knobs display at **112 px diameter** with a **128 px interaction target**. Small knobs display at **82 px** with a **96 px target**. Buttons display at **184 × 58 px**. Corner screws display at **34 px**, inset **24 px**.

For responsive scaling: `scale = min(viewportWidth / 2048, viewportHeight / 2048)`. Multiply all reference measurements by `scale`. Keep images proportional.

Use the knob PNG corresponding to a fixed state, or use one knob image and rotate it from **−135° to +135°**. Rotation is the lighter-weight app approach.

Typography: **Bahnschrift Condensed SemiBold**. Open-source alternatives: **Oswald SemiBold** or **Barlow Condensed SemiBold**. Use live text in the app for accessibility and localization; `font-style-preview.png` is only a style reference.

The generated sheets are included alongside individual crops so you can recrop or adjust assets later.
