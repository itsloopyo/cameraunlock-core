using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Offsets a uGUI element (typically the game's own crosshair) from its original
    /// anchoredPosition by a screen-pixel amount, converting through the canvas scale
    /// factor. The original position is restored whenever compensation stops.
    ///
    /// The target GameObject is resolved lazily through a game-specific resolver and
    /// re-resolved whenever the cached reference goes stale (destroyed on scene change).
    /// </summary>
    public class AnchoredOffsetCompensator
    {
        private readonly Func<GameObject> _resolveTarget;

        private RectTransform _targetRect;
        private Canvas _canvas;
        private RectTransform _canvasRect;
        private Vector2 _originalAnchoredPosition;
        private bool _hasOriginal;
        private bool _isOffset;
        private GameObject _cachedTarget;

        /// <param name="resolveTarget">
        /// Returns the UI GameObject to offset, or null when it is not available
        /// (menus, loading, game UI not yet created).
        /// </param>
        public AnchoredOffsetCompensator(Func<GameObject> resolveTarget)
        {
            if (resolveTarget == null) throw new ArgumentNullException("resolveTarget");
            _resolveTarget = resolveTarget;
        }

        public void ApplyOffset(Vector2 screenPixelOffset)
        {
            if (!EnsureTarget())
                return;

            float scale = _canvas != null ? _canvas.scaleFactor : 1f;
            if (scale <= 0f) scale = 1f;

            ApplyLocalOffset(screenPixelOffset / scale);
        }

        /// <summary>
        /// Offsets the element by a normalized-device-coordinate amount (±1 at the edges of the
        /// view), scaled by the canvas's own rect.
        ///
        /// Prefer this over <see cref="ApplyOffset"/>. Screen pixels are only the correct unit
        /// for a screen-space-overlay canvas filling the window: a world-space HUD canvas has no
        /// meaningful scaleFactor, and a game rendering through a render texture whose aspect
        /// differs from the window makes Screen.width the wrong divisor. Going through the canvas
        /// rect is right in every one of those cases, and reduces to the same result as
        /// ApplyOffset for a plain overlay canvas.
        /// </summary>
        public void ApplyNdcOffset(Vector2 ndcOffset)
        {
            if (!EnsureTarget())
                return;

            if (_canvasRect == null)
                return;

            Rect canvasArea = _canvasRect.rect;
            ApplyLocalOffset(new Vector2(
                ndcOffset.x * canvasArea.width * 0.5f,
                ndcOffset.y * canvasArea.height * 0.5f));
        }

        private void ApplyLocalOffset(Vector2 localOffset)
        {
            if (!_hasOriginal)
            {
                _originalAnchoredPosition = _targetRect.anchoredPosition;
                _hasOriginal = true;
            }

            _targetRect.anchoredPosition = _originalAnchoredPosition + localOffset;
            _isOffset = true;
        }

        public void Restore()
        {
            if (!_isOffset)
                return;

            _isOffset = false;
            if (_targetRect != null && _hasOriginal)
                _targetRect.anchoredPosition = _originalAnchoredPosition;
        }

        private bool EnsureTarget()
        {
            if (_cachedTarget != null && _targetRect != null)
                return true;

            var target = _resolveTarget();
            if (target == null)
            {
                _cachedTarget = null;
                _targetRect = null;
                return false;
            }

            if (target != _cachedTarget || _targetRect == null)
            {
                _cachedTarget = target;
                _targetRect = target.GetComponent<RectTransform>();
                _canvas = target.GetComponentInParent<Canvas>();
                _canvasRect = _canvas != null ? _canvas.GetComponent<RectTransform>() : null;
                _hasOriginal = false;
                _isOffset = false;
            }

            return _targetRect != null;
        }
    }
}
