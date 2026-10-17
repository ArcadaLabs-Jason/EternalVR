using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher
{
    /// <summary>
    /// A button with a brand's mark beside its words (<see cref="Branding"/>): the mark is the brand's file, drawn at the
    /// height of the button's text and redrawn when its font changes (as it does with the display's scale). Where the
    /// words do not fit (<see cref="Compact"/>) it shows the mark alone; its accessible name and tooltip keep the words.
    /// </summary>
    internal sealed class MarkButton : Button
    {
        private readonly Image mark;
        private readonly string words;
        private bool compact;

        /// <param name="height">The button's height at 96 DPI (the bar's buttons are all one height).</param>
        public MarkButton(string text, Image mark, int height)
        {
            this.mark = mark;
            words = text;
            MinimumSize = new Size(0, height);
            Text = text;
            AccessibleName = text;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;
            TextImageRelation = TextImageRelation.ImageBeforeText;
            ImageAlign = ContentAlignment.MiddleLeft;
            TextAlign = ContentAlignment.MiddleLeft;
            Padding = new Padding(4, 0, 4, 0);
        }

        /// <summary>The mark alone, in a square button (no words); off shows the mark and the words. Without a mark, never.</summary>
        public bool Compact
        {
            get => compact;
            set
            {
                value &= mark != null;
                if (value == compact) return;
                compact = value;
                Text = value ? string.Empty : words;
                ImageAlign = value ? ContentAlignment.MiddleCenter : ContentAlignment.MiddleLeft;
                MinimumSize = new Size(value ? MinimumSize.Height : 0, MinimumSize.Height);
            }
        }

        /// <summary>The button's width with its words, whether or not they are shown now.</summary>
        public int WidthWithWords =>
            compact ? Math.Max(MinimumSize.Height, GetPreferredSize(Size.Empty).Width) + TextRenderer.MeasureText(words, Font).Width + LogicalToDeviceUnits(8)
                    : GetPreferredSize(Size.Empty).Width;

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            DrawMark();
        }

        protected override void OnFontChanged(EventArgs e)
        {
            base.OnFontChanged(e);
            if (IsHandleCreated) DrawMark();
        }

        /// <summary>The mark at the text's height, its shape kept.</summary>
        private void DrawMark()
        {
            if (mark == null) return;
            int h = Math.Max(8, Font.Height);
            int w = Math.Max(1, (int)Math.Round(h * (double)mark.Width / mark.Height));
            var bitmap = new Bitmap(w, h);
            using (var g = Graphics.FromImage(bitmap))
            {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.DrawImage(mark, new Rectangle(0, 0, w, h));
            }
            var old = Image;
            Image = bitmap;
            old?.Dispose();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                Image?.Dispose();
                mark?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>The two report links (<see cref="ReportLinks"/>): GitHub issues and the Discord bug report channel.</summary>
    internal static class ReportLinkButtons
    {
        public static MarkButton GitHub(int height, ToolTip tips, Action<string> logError) =>
            Make(ReportLinks.GitHubText, Branding.GitHubMark(), height, ReportLinks.GitHubIssues, ReportLinks.GitHubTooltip, tips, logError);

        public static MarkButton Discord(int height, ToolTip tips, Action<string> logError) =>
            Make(ReportLinks.DiscordText, Branding.DiscordSymbol(), height, ReportLinks.Discord, ReportLinks.DiscordTooltip, tips, logError);

        private static MarkButton Make(string text, Image mark, int height, string url, string tooltip, ToolTip tips, Action<string> logError)
        {
            var button = new MarkButton(text, mark, height);
            button.Click += (s, e) => Open(button.FindForm(), url, logError);
            // The words lead the tooltip: a compact button shows the mark alone.
            tips?.SetToolTip(button, text + ": " + tooltip);
            return button;
        }

        /// <summary>
        /// Opens <paramref name="url"/> in the default browser (<see cref="ReportLinks.Open"/>). When nothing opens it, the
        /// player is told so, with the link copied to the clipboard where that works, and the failure is logged.
        /// </summary>
        public static void Open(IWin32Window owner, string url, Action<string> logError)
        {
            if (ReportLinks.Open(url, logError)) return;
            bool copied = false;
            try
            {
                Clipboard.SetText(url);
                copied = true;
            }
            catch (Exception e) when (e is ExternalException || e is ThreadStateException)
            {
                logError?.Invoke("could not copy the link to the clipboard: " + e.Message);
            }
            MessageBox.Show(owner,
                "No browser opened the link. " + (copied ? "It is copied: paste it into your browser's address bar." : "Type it into your browser's address bar:")
                + "\n\n" + url,
                "Open link", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }
}
