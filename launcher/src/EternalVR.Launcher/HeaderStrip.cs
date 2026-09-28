using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The window's top: the header strip (branding/launcher-header-1440x240.png) at the strip's height, left-aligned on
    /// black, with the game's state and the launcher's version in its dark right third.
    /// </summary>
    internal sealed class HeaderStrip : Control
    {
        private readonly Image image;
        private string game = string.Empty, build = string.Empty, version = string.Empty;
        private Color buildColor = Color.Gainsboro;

        public HeaderStrip(Image image)
        {
            this.image = image;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
            BackColor = Color.FromArgb(10, 8, 8);
        }

        /// <summary>The lines in the right third: where the game is, its build's state (good or not) and the version.</summary>
        public void SetText(string gameLine, string buildLine, bool buildGood, string versionLine)
        {
            game = gameLine ?? string.Empty;
            build = buildLine ?? string.Empty;
            buildColor = buildGood ? Color.FromArgb(120, 210, 120) : Color.FromArgb(240, 160, 60);
            version = versionLine ?? string.Empty;
            AccessibleName = string.Join(". ", game, build, version);
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            if (image != null)
            {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                int h = ClientSize.Height;
                g.DrawImage(image, 0, 0, image.Width * h / image.Height, h);
            }
            // The strip's own art ends two thirds in: the text keeps to the right third.
            int pad = Font.Height / 2;
            int left = System.Math.Max(ClientSize.Width * 2 / 3, ClientSize.Width - Font.Height * 16);
            var area = new Rectangle(left, pad, ClientSize.Width - left - pad, ClientSize.Height - 2 * pad);
            const TextFormatFlags flags = TextFormatFlags.Right | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix;
            int line = Font.Height + 2;
            int top = area.Top + (area.Height - 3 * line) / 2;
            TextRenderer.DrawText(g, game, Font, new Rectangle(area.Left, top, area.Width, line), Color.Gainsboro, flags);
            TextRenderer.DrawText(g, build, Font, new Rectangle(area.Left, top + line, area.Width, line), buildColor, flags);
            TextRenderer.DrawText(g, version, Font, new Rectangle(area.Left, top + 2 * line, area.Width, line), Color.Gray, flags);
        }
    }
}
