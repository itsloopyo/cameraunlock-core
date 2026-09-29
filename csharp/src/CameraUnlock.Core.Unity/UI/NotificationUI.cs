using UnityEngine;

namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Displays brief on-screen notifications for toggle/recenter actions.
    /// Supports different notification types with color-coded feedback.
    /// IMGUI-based, so no canvas setup is required.
    /// </summary>
    public class NotificationUI
    {
        private const float DefaultDisplayDuration = 2.0f;
        private const float FadeDuration = 0.5f;
        private const int FontSize = 24;
        private const int ShadowOffset = 2;
        private const float VerticalPosition = 100f;

        private string _currentMessage;
        private float _displayTimer;
        private NotificationType _currentType;
        private GUIStyle _textStyle;
        private GUIStyle _shadowStyle;
        private bool _stylesInitialized;

        // Colors for different notification types
        private static readonly Color InfoColor = Color.white;
        private static readonly Color SuccessColor = new Color(0.4f, 1.0f, 0.4f); // Light green
        private static readonly Color WarningColor = new Color(1.0f, 0.9f, 0.3f); // Yellow
        private static readonly Color ErrorColor = new Color(1.0f, 0.4f, 0.4f);   // Light red

        /// <summary>
        /// Whether a notification is currently being displayed.
        /// </summary>
        public bool IsDisplaying
        {
            get { return _displayTimer > 0f && !string.IsNullOrEmpty(_currentMessage); }
        }

        /// <summary>
        /// Display a notification message with default duration and info type.
        /// </summary>
        public void ShowNotification(string message)
        {
            ShowNotification(message, NotificationType.Info, DefaultDisplayDuration);
        }

        /// <summary>
        /// Display a notification message with custom duration.
        /// </summary>
        /// <param name="message">The message to display.</param>
        /// <param name="duration">Display duration in seconds (before fade begins).</param>
        public void ShowNotification(string message, float duration)
        {
            ShowNotification(message, NotificationType.Info, duration);
        }

        /// <summary>
        /// Display a notification message with specified type.
        /// </summary>
        /// <param name="message">The message to display.</param>
        /// <param name="type">The notification type for styling.</param>
        public void ShowNotification(string message, NotificationType type)
        {
            ShowNotification(message, type, DefaultDisplayDuration);
        }

        /// <summary>
        /// Display a notification message with custom type and duration.
        /// </summary>
        /// <param name="message">The message to display.</param>
        /// <param name="type">The notification type for styling.</param>
        /// <param name="duration">Display duration in seconds (before fade begins).</param>
        public void ShowNotification(string message, NotificationType type, float duration)
        {
            _currentMessage = message;
            _currentType = type;
            _displayTimer = duration + FadeDuration;
        }

        /// <summary>
        /// Display a tracking enabled notification.
        /// </summary>
        public void ShowTrackingEnabled()
        {
            ShowNotification("Head Tracking: ON", NotificationType.Success);
        }

        /// <summary>
        /// Display a tracking disabled notification.
        /// </summary>
        public void ShowTrackingDisabled()
        {
            ShowNotification("Head Tracking: OFF", NotificationType.Warning);
        }

        /// <summary>
        /// Display a recentered notification.
        /// </summary>
        public void ShowRecentered()
        {
            ShowNotification("Head Tracking: Recentered", NotificationType.Info);
        }

        /// <summary>
        /// Display a connection established notification.
        /// </summary>
        public void ShowConnectionEstablished()
        {
            ShowNotification("OpenTrack: Connected", NotificationType.Success);
        }

        /// <summary>
        /// Display a connection lost notification.
        /// </summary>
        public void ShowConnectionLost()
        {
            ShowNotification("OpenTrack: No Signal", NotificationType.Warning);
        }

        /// <summary>
        /// Update the notification timer. Call from Update.
        /// </summary>
        public void Update()
        {
            if (_displayTimer > 0f)
            {
                // Unscaled: a toast raised in a pause menu (timeScale 0) would otherwise
                // stay on screen until the game resumes.
                _displayTimer -= Time.unscaledDeltaTime;
            }
        }

        /// <summary>
        /// Draw the notification. Call from OnGUI.
        /// </summary>
        public void Draw()
        {
            if (_displayTimer <= 0f || string.IsNullOrEmpty(_currentMessage))
            {
                return;
            }

            EnsureStyles();

            // Calculate alpha for fade out
            float alpha = 1f;
            if (_displayTimer < FadeDuration)
            {
                alpha = _displayTimer / FadeDuration;
            }

            // Get color based on notification type
            Color textColor = GetColorForType(_currentType);

            // Calculate position (top-center)
            GUIContent content = new GUIContent(_currentMessage);
            Vector2 size = _textStyle.CalcSize(content);
            float x = (Screen.width - size.x) / 2f;
            float y = VerticalPosition;

            Rect rect = new Rect(x, y, size.x, size.y);
            Rect shadowRect = new Rect(x + ShadowOffset, y + ShadowOffset, size.x, size.y);

            // Draw shadow first, then text
            _shadowStyle.normal.textColor = new Color(0f, 0f, 0f, alpha * 0.8f);
            GUI.Label(shadowRect, _currentMessage, _shadowStyle);

            _textStyle.normal.textColor = new Color(textColor.r, textColor.g, textColor.b, alpha);
            GUI.Label(rect, _currentMessage, _textStyle);
        }

        private static Color GetColorForType(NotificationType type)
        {
            switch (type)
            {
                case NotificationType.Success:
                    return SuccessColor;
                case NotificationType.Warning:
                    return WarningColor;
                case NotificationType.Error:
                    return ErrorColor;
                case NotificationType.Info:
                default:
                    return InfoColor;
            }
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
                alignment = TextAnchor.MiddleCenter
            };
            _textStyle.normal.textColor = Color.white;

            _shadowStyle = new GUIStyle(_textStyle);
            _shadowStyle.normal.textColor = Color.black;

            _stylesInitialized = true;
        }
    }
}
