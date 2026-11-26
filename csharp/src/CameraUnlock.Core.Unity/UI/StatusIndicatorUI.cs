using UnityEngine;

namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Displays a persistent status indicator showing tracking state and connection status.
    /// Optimized with cached GUIContent and pre-calculated sizes.
    /// IMGUI-based, so no canvas setup is required.
    /// </summary>
    public class StatusIndicatorUI
    {
        private const int FontSize = 14;
        private const int Padding = 10;
        private const int ShadowOffset = 1;
        private const float BackgroundAlpha = 0.4f;

        private GUIStyle _textStyle;
        private GUIStyle _shadowStyle;
        private GUIStyle _backgroundStyle;
        private Texture2D _backgroundTexture;
        private bool _stylesInitialized;

        private bool _enabled = true;
        private StatusPosition _position = StatusPosition.BottomRight;

        // Current state
        private bool _trackingEnabled;
        private bool _isReceiving;

        // Cached state for dirty checking
        private bool _cachedTrackingEnabled;
        private bool _cachedIsReceiving;

        // Pre-calculated layout values
        private string _statusText;
        private string _trackingStatus;
        private string _connectionStatus;
        private float _width;
        private float _height;
        private float _textWidth;
        private float _textHeight;
        private float _offset1;
        private float _offset2;
        private float _offset3;
        private float _offset4;
        private bool _layoutDirty = true;

        // Cached strings to avoid allocation
        private const string PrefixHT = "[HT:";
        private const string PrefixOT = "] [OT:";
        private const string Suffix = "]";
        private const string StatusOn = "ON";
        private const string StatusOff = "OFF";
        private const string ConnOk = "OK";
        private const string ConnNone = "--";

        // Colors
        private static readonly Color EnabledColor = new Color(0.4f, 1.0f, 0.4f);  // Green
        private static readonly Color DisabledColor = new Color(0.6f, 0.6f, 0.6f); // Gray
        private static readonly Color ConnectedColor = new Color(0.4f, 1.0f, 0.4f); // Green
        private static readonly Color DisconnectedColor = new Color(1.0f, 0.5f, 0.3f); // Orange

        /// <summary>
        /// Whether the status indicator is enabled.
        /// </summary>
        public bool Enabled
        {
            get { return _enabled; }
            set { _enabled = value; }
        }

        /// <summary>
        /// Screen position for the status indicator.
        /// </summary>
        public StatusPosition Position
        {
            get { return _position; }
            set
            {
                if (_position != value)
                {
                    _position = value;
                    _layoutDirty = true;
                }
            }
        }

        /// <summary>
        /// Update the tracking state display.
        /// </summary>
        public void UpdateState(bool trackingEnabled, bool isReceiving)
        {
            _trackingEnabled = trackingEnabled;
            _isReceiving = isReceiving;

            // Mark layout dirty if state changed
            if (_trackingEnabled != _cachedTrackingEnabled || _isReceiving != _cachedIsReceiving)
            {
                _layoutDirty = true;
            }
        }

        /// <summary>
        /// Draw the status indicator. Call from OnGUI.
        /// </summary>
        public void Draw()
        {
            if (!_enabled)
            {
                return;
            }

            EnsureStyles();

            // Recalculate layout only when state changes
            if (_layoutDirty)
            {
                RecalculateLayout();
            }

            // Calculate position based on corner setting
            Rect backgroundRect = CalculatePosition(_width, _height);
            Rect textRect = new Rect(
                backgroundRect.x + Padding,
                backgroundRect.y + Padding / 2f,
                _textWidth,
                _textHeight
            );
            Rect shadowRect = new Rect(
                textRect.x + ShadowOffset,
                textRect.y + ShadowOffset,
                _textWidth,
                _textHeight
            );

            // Draw semi-transparent background
            GUI.Box(backgroundRect, GUIContent.none, _backgroundStyle);

            // Draw shadow
            _shadowStyle.normal.textColor = new Color(0f, 0f, 0f, 0.6f);
            GUI.Label(shadowRect, _statusText, _shadowStyle);

            // Draw status with color-coded parts (using cached offsets)
            DrawColoredStatus(textRect);
        }

        /// <summary>
        /// Clean up resources.
        /// </summary>
        public void Dispose()
        {
            if (_backgroundTexture != null)
            {
                Object.Destroy(_backgroundTexture);
                _backgroundTexture = null;
            }
        }

        private void RecalculateLayout()
        {
            _trackingStatus = _trackingEnabled ? StatusOn : StatusOff;
            _connectionStatus = _isReceiving ? ConnOk : ConnNone;
            _statusText = string.Format("{0}{1}{2}{3}{4}", PrefixHT, _trackingStatus, PrefixOT, _connectionStatus, Suffix);

            // Calculate sizes
            GUIContent content = new GUIContent(_statusText);
            Vector2 size = _textStyle.CalcSize(content);
            _textWidth = size.x;
            _textHeight = size.y;
            _width = _textWidth + Padding * 2;
            _height = _textHeight + Padding;

            // Pre-calculate offsets for colored segments
            _offset1 = _textStyle.CalcSize(new GUIContent(PrefixHT)).x;
            _offset2 = _offset1 + _textStyle.CalcSize(new GUIContent(_trackingStatus)).x;
            _offset3 = _offset2 + _textStyle.CalcSize(new GUIContent(PrefixOT)).x;
            _offset4 = _offset3 + _textStyle.CalcSize(new GUIContent(_connectionStatus)).x;

            // Cache state
            _cachedTrackingEnabled = _trackingEnabled;
            _cachedIsReceiving = _isReceiving;
            _layoutDirty = false;
        }

        private void DrawColoredStatus(Rect rect)
        {
            Color trackingColor = _trackingEnabled ? EnabledColor : DisabledColor;
            Color connectionColor = _isReceiving ? ConnectedColor : DisconnectedColor;

            // Draw [HT: in white
            _textStyle.normal.textColor = Color.white;
            GUI.Label(rect, PrefixHT, _textStyle);

            // Draw tracking status with color
            Rect trackingRect = new Rect(rect.x + _offset1, rect.y, rect.width - _offset1, rect.height);
            _textStyle.normal.textColor = trackingColor;
            GUI.Label(trackingRect, _trackingStatus, _textStyle);

            // Draw ] [OT: in white
            Rect midRect = new Rect(rect.x + _offset2, rect.y, rect.width - _offset2, rect.height);
            _textStyle.normal.textColor = Color.white;
            GUI.Label(midRect, PrefixOT, _textStyle);

            // Draw connection status with color
            Rect connRect = new Rect(rect.x + _offset3, rect.y, rect.width - _offset3, rect.height);
            _textStyle.normal.textColor = connectionColor;
            GUI.Label(connRect, _connectionStatus, _textStyle);

            // Draw final ] in white
            Rect suffixRect = new Rect(rect.x + _offset4, rect.y, rect.width - _offset4, rect.height);
            _textStyle.normal.textColor = Color.white;
            GUI.Label(suffixRect, Suffix, _textStyle);
        }

        private Rect CalculatePosition(float width, float height)
        {
            float x, y;

            switch (_position)
            {
                case StatusPosition.TopLeft:
                    x = Padding;
                    y = Padding;
                    break;
                case StatusPosition.TopRight:
                    x = Screen.width - width - Padding;
                    y = Padding;
                    break;
                case StatusPosition.BottomLeft:
                    x = Padding;
                    y = Screen.height - height - Padding;
                    break;
                case StatusPosition.BottomRight:
                default:
                    x = Screen.width - width - Padding;
                    y = Screen.height - height - Padding;
                    break;
            }

            return new Rect(x, y, width, height);
        }

        private void EnsureStyles()
        {
            if (_stylesInitialized)
            {
                return;
            }

            _textStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = FontSize,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            _textStyle.normal.textColor = Color.white;

            _shadowStyle = new GUIStyle(_textStyle);
            _shadowStyle.normal.textColor = Color.black;

            _backgroundStyle = new GUIStyle(GUI.skin.box);
            _backgroundTexture = new Texture2D(1, 1);
            _backgroundTexture.SetPixel(0, 0, new Color(0f, 0f, 0f, BackgroundAlpha));
            _backgroundTexture.Apply();
            _backgroundStyle.normal.background = _backgroundTexture;

            _stylesInitialized = true;

            // Force layout recalculation after styles are ready
            _layoutDirty = true;
        }
    }
}
