using System;
using System.Collections.Generic;
using CameraUnlock.Core.Unity.Extensions;
using UnityEngine;

namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Compensates every direct child of a canvas for head tracking rotation, holding each
    /// child's authored anchoredPosition so the write is always original + this frame's
    /// offset. This is the stateful replacement for the obsolete
    /// <c>CanvasCompensation.RepositionChildren</c>, which had nowhere to store the originals
    /// and so accumulated its own offset on every call.
    ///
    /// Originals are re-captured whenever the canvas's child set changes (scene change,
    /// HUD rebuild), because the cached RectTransforms are then stale.
    /// </summary>
    public sealed class CanvasChildrenCompensator
    {
        private readonly List<RectTransform> _children = new List<RectTransform>();
        private readonly List<Vector2> _originals = new List<Vector2>();

        private RectTransform _canvasRect;
        private int _capturedChildCount = -1;
        private bool _isOffset;

        /// <summary>
        /// Applies the compensation to all active RectTransform children of the canvas.
        /// </summary>
        /// <param name="canvasRectTransform">The canvas RectTransform whose children to reposition.</param>
        /// <param name="cam">The camera to get FOV information from.</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <exception cref="ArgumentNullException">Thrown when canvasRectTransform or cam is null.</exception>
        public void Apply(RectTransform canvasRectTransform, Camera cam, float yaw, float pitch, float roll)
        {
            if (canvasRectTransform == null)
            {
                throw new ArgumentNullException(nameof(canvasRectTransform));
            }

            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            Capture(canvasRectTransform);

            float canvasWidth = canvasRectTransform.rect.width;
            float canvasHeight = canvasRectTransform.rect.height;

            for (int i = 0; i < _children.Count; i++)
            {
                RectTransform child = _children[i];
                if (child == null || !child.gameObject.activeInHierarchy)
                {
                    continue;
                }

                CanvasCompensation.RepositionElement(
                    child, _originals[i], canvasWidth, canvasHeight, cam, yaw, pitch, roll);
            }

            _isOffset = true;
        }

        /// <summary>
        /// Returns every captured child to its original anchoredPosition. Safe to call when
        /// nothing has been offset.
        /// </summary>
        public void Restore()
        {
            if (!_isOffset)
            {
                return;
            }

            _isOffset = false;

            for (int i = 0; i < _children.Count; i++)
            {
                RectTransform child = _children[i];
                if (child != null)
                {
                    child.anchoredPosition = _originals[i];
                }
            }
        }

        /// <summary>
        /// Drops the cached children and originals so the next Apply re-captures. Call after
        /// changing the canvas contents yourself.
        /// </summary>
        public void Invalidate()
        {
            _children.Clear();
            _originals.Clear();
            _canvasRect = null;
            _capturedChildCount = -1;
            _isOffset = false;
        }

        private void Capture(RectTransform canvasRectTransform)
        {
            if (_canvasRect == canvasRectTransform
                && _capturedChildCount == canvasRectTransform.childCount
                && ChildrenStillAlive())
            {
                return;
            }

            // Put the surviving children back on their ORIGINALS before re-capturing.
            // anchoredPosition currently reads original + offset, so capturing it as the
            // new original folds one offset in permanently - and then clearing _isOffset
            // discards the only record needed to undo it. A HUD element that toggles
            // visibility (a damage flash, an ammo counter) changes childCount twice per
            // appearance, so this accumulated on every toggle and walked the HUD off
            // screen: exactly the unbounded drift this class exists to prevent.
            Restore();

            _children.Clear();
            _originals.Clear();
            _canvasRect = canvasRectTransform;
            _capturedChildCount = canvasRectTransform.childCount;
            _isOffset = false;

            for (int i = 0; i < _capturedChildCount; i++)
            {
                RectTransform rectTransform = canvasRectTransform.GetChild(i) as RectTransform;
                if (rectTransform == null)
                {
                    continue;
                }

                _children.Add(rectTransform);
                _originals.Add(rectTransform.anchoredPosition);
            }
        }

        // A HUD rebuilt with the same child count leaves every cached RectTransform destroyed
        // while the count check still says "unchanged", so the new elements would never be
        // offset. Unity's fake null is the only thing that reveals it.
        private bool ChildrenStillAlive()
        {
            for (int i = 0; i < _children.Count; i++)
            {
                if (_children[i] == null) return false;
            }
            return true;
        }
    }
}
