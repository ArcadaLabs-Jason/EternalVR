using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The window's top: the header strip (branding/launcher-header-1440x240.png) at the strip's height, left-aligned on
    /// black, with the game's state, the launcher's version and the Ko-fi support link in its dark right third.
    /// </summary>
    internal sealed class HeaderStrip : Control
    {
        private const string SupportText = "Support EternalVR on Ko-fi";
        private readonly Image image, cup;
        private string game = string.Empty, build = string.Empty, version = string.Empty;
        private Color buildColor = Color.Gainsboro;
        /// <summary>Where the support link (cup and text) was last drawn, and whether the mouse is over it.</summary>
        private Rectangle supportArea;
        private bool supportHot;

        public HeaderStrip(Image image, Image cup)
        {
            this.image = image;
            this.cup = cup;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
            BackColor = Color.FromArgb(10, 8, 8);
        }

        /// <summary>The support link was clicked.</summary>
        public event EventHandler SupportClicked;

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
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            if (image != null)
            {
                int h = ClientSize.Height;
                g.DrawImage(image, 0, 0, image.Width * h / image.Height, h);
            }
            // The strip's own art ends two thirds in: the text keeps to the right third.
            int pad = Font.Height / 2;
            int left = Math.Max(ClientSize.Width * 2 / 3, ClientSize.Width - Font.Height * 16);
            var area = new Rectangle(left, pad, ClientSize.Width - left - pad, ClientSize.Height - 2 * pad);
            const TextFormatFlags flags = TextFormatFlags.Right | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix;
            int line = Font.Height + 2;
            // Three lines, then the support link half a line below them.
            int top = area.Top + (area.Height - 4 * line - line / 2) / 2;
            TextRenderer.DrawText(g, game, Font, new Rectangle(area.Left, top, area.Width, line), Color.Gainsboro, flags);
            TextRenderer.DrawText(g, build, Font, new Rectangle(area.Left, top + line, area.Width, line), buildColor, flags);
            TextRenderer.DrawText(g, version, Font, new Rectangle(area.Left, top + 2 * line, area.Width, line), Color.Gray, flags);
            DrawSupportLink(g, area.Right, top + 3 * line + line / 2, line);
        }

        /// <summary>Ko-fi's cup and the link's text, right-aligned at <paramref name="right"/>; underlined under the mouse.</summary>
        private void DrawSupportLink(Graphics g, int right, int top, int line)
        {
            using (var font = new Font(Font, supportHot ? FontStyle.Underline : FontStyle.Regular))
            {
                int textWidth = TextRenderer.MeasureText(g, SupportText, font, Size.Empty, TextFormatFlags.NoPrefix).Width;
                int cupHeight = line + 2;
                int cupWidth = cup == null ? 0 : cup.Width * cupHeight / cup.Height;
                int gap = cup == null ? 0 : Font.Height / 4;
                supportArea = new Rectangle(right - textWidth - gap - cupWidth, top - 1, cupWidth + gap + textWidth, cupHeight);
                if (cup != null) g.DrawImage(cup, supportArea.Left, supportArea.Top, cupWidth, cupHeight);
                TextRenderer.DrawText(g, SupportText, font, new Rectangle(right - textWidth, top, textWidth, line),
                    supportHot ? Color.White : Color.Silver, TextFormatFlags.Right | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
            }
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            SetSupportHot(supportArea.Contains(e.Location));
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            SetSupportHot(false);
        }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            base.OnMouseClick(e);
            if (e.Button == MouseButtons.Left && supportArea.Contains(e.Location)) SupportClicked?.Invoke(this, EventArgs.Empty);
        }

        private void SetSupportHot(bool hot)
        {
            if (supportHot == hot) return;
            supportHot = hot;
            Cursor = hot ? Cursors.Hand : Cursors.Default;
            Invalidate();
        }
    }
}
