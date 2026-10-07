using System;
using System.Runtime.InteropServices;

namespace CameraUnlock.Core.Os
{
    /// <summary>
    /// Where the game's window sits on its monitor. The C# half of
    /// cameraunlock/os/game_window.h.
    /// </summary>
    public static class GameWindow
    {
        private const int GwlStyle = -16;
        private const int WsCaption = 0x00C00000;
        private const uint MonitorDefaultToNearest = 0x0002;
        private const uint SwpNoSize = 0x0001;
        private const uint SwpNoZOrder = 0x0004;
        private const uint SwpNoActivate = 0x0010;

        /// <summary>
        /// Where a window <paramref name="windowLength"/> long starts, on one axis, to sit
        /// in the middle of a work area <paramref name="workLength"/> long that starts at
        /// <paramref name="workStart"/>. Call it once for x and once for y.
        /// <paramref name="workStart"/> is not zero on a secondary monitor and is negative
        /// on one left of or above the primary.
        /// <para>
        /// The half is rounded toward zero. An odd gap leaves the spare pixel after the
        /// window, and a window longer than the area starts before
        /// <paramref name="workStart"/>: a caller that must not move such a window checks
        /// the lengths first, as <see cref="CenterWindowInWorkArea"/> does.
        /// </para>
        /// </summary>
        public static int CenteredOrigin(int workStart, int workLength, int windowLength)
        {
            return workStart + (workLength - windowLength) / 2;
        }

        /// <summary>
        /// Centres <paramref name="window"/> in the work area of the monitor it is on, and
        /// can be called after every placement: when the game creates its window, changes
        /// its windowed resolution or comes back from fullscreen. It keeps no state, moves
        /// the window and nothing else (no resize, no activation, no z-order change), and
        /// does not look for the window: the caller passes the one it means.
        /// <para>
        /// True when the window is at the centred origin on return, whether this call moved
        /// it or it was there already. False when it was left where it was: the handle is
        /// not a window, it is minimised, maximised or has no caption (borderless and
        /// exclusive fullscreen), it is wider or taller than the work area, or a Win32 call
        /// failed.
        /// </para>
        /// <para>
        /// A move logs the window's size, the work area and both origins. A window left
        /// alone logs why. A window already centred logs nothing, since a game may place
        /// its window many times. <paramref name="log"/> may be null.
        /// </para>
        /// </summary>
#if NULLABLE_ENABLED
        public static bool CenterWindowInWorkArea(IntPtr window, Action<string>? log)
#else
        public static bool CenterWindowInWorkArea(IntPtr window, Action<string> log)
#endif
        {
            if (!IsWindow(window))
            {
                Emit(log, "window: 0x" + window.ToInt64().ToString("X") + " is not a window");
                return false;
            }
            // A minimised window reports a small rect far off screen in place of its own.
            if (IsIconic(window))
            {
                Emit(log, "window: minimised, leaving position unchanged");
                return false;
            }
            if (IsZoomed(window))
            {
                Emit(log, "window: maximised, leaving position unchanged");
                return false;
            }
            if ((GetWindowLongW(window, GwlStyle) & WsCaption) == 0)
            {
                Emit(log, "window: borderless/fullscreen window, leaving position unchanged");
                return false;
            }

            NativeRect rect;
            if (!GetWindowRect(window, out rect))
            {
                Emit(log, "window: GetWindowRect failed: " + Marshal.GetLastWin32Error());
                return false;
            }
            var info = new MonitorInfo { Size = Marshal.SizeOf(typeof(MonitorInfo)) };
            if (!GetMonitorInfoW(MonitorFromWindow(window, MonitorDefaultToNearest), ref info))
            {
                Emit(log, "window: GetMonitorInfoW failed");
                return false;
            }
            NativeRect work = info.Work;
            int windowWidth = rect.Right - rect.Left;
            int windowHeight = rect.Bottom - rect.Top;
            int workWidth = work.Right - work.Left;
            int workHeight = work.Bottom - work.Top;

            // Nowhere on the work area shows all of it, and centring a window taller than
            // the work area puts its title bar above the top, out of reach.
            if (windowWidth > workWidth || windowHeight > workHeight)
            {
                Emit(log, "window: " + windowWidth + "x" + windowHeight + " at (" + rect.Left + ", " + rect.Top
                    + ") does not fit work area " + workWidth + "x" + workHeight + " at (" + work.Left + ", " + work.Top
                    + "), leaving position unchanged");
                return false;
            }

            int x = CenteredOrigin(work.Left, workWidth, windowWidth);
            int y = CenteredOrigin(work.Top, workHeight, windowHeight);
            if (x == rect.Left && y == rect.Top) return true;

            if (!SetWindowPos(window, IntPtr.Zero, x, y, 0, 0, SwpNoSize | SwpNoZOrder | SwpNoActivate))
            {
                Emit(log, "window: SetWindowPos failed: " + Marshal.GetLastWin32Error());
                return false;
            }
            Emit(log, "window: centered " + windowWidth + "x" + windowHeight + " at (" + x + ", " + y
                + ") on work area " + workWidth + "x" + workHeight + " at (" + work.Left + ", " + work.Top
                + "), was at (" + rect.Left + ", " + rect.Top + ")");
            return true;
        }

#if NULLABLE_ENABLED
        private static void Emit(Action<string>? log, string message)
#else
        private static void Emit(Action<string> log, string message)
#endif
        {
            if (log != null) log(message);
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeRect
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MonitorInfo
        {
            public int Size;
            public NativeRect Monitor;
            public NativeRect Work;
            public uint Flags;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindow(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsIconic(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsZoomed(IntPtr window);

        [DllImport("user32.dll")]
        private static extern int GetWindowLongW(IntPtr window, int index);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetWindowRect(IntPtr window, out NativeRect rect);

        [DllImport("user32.dll")]
        private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    }
}
