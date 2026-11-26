using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Utilities
{
    /// <summary>
    /// Per-frame caching utility that ensures values are only computed once per frame.
    /// Useful for expensive reflection lookups or game state detection.
    /// Zero-allocation after initial setup.
    /// </summary>
    /// <typeparam name="T">Type of value to cache.</typeparam>
    public sealed class PerFrameCache<T>
    {
        private T _cachedValue;
        private int _cachedFrameCount;
        private readonly Func<T> _fetcher;
        private bool _hasValue;

        /// <summary>
        /// Creates a new per-frame cache with a value fetcher function.
        /// </summary>
        /// <param name="fetcher">Function that fetches the value when cache is invalid.</param>
        /// <exception cref="ArgumentNullException">Thrown when fetcher is null.</exception>
        public PerFrameCache(Func<T> fetcher)
        {
            if (fetcher == null) throw new ArgumentNullException("fetcher");
            _fetcher = fetcher;
            _cachedFrameCount = -1;
        }

        /// <summary>
        /// Creates a new per-frame cache without a default fetcher.
        /// Use <see cref="Get(Func{T})"/> to provide the fetcher on each call.
        /// </summary>
        public PerFrameCache()
        {
            _fetcher = null;
            _cachedFrameCount = -1;
        }

        /// <summary>
        /// Gets the cached value, refreshing if this is a new frame.
        /// Uses the fetcher provided in the constructor.
        /// </summary>
        public T Value
        {
            get { return Get(); }
        }

        /// <summary>
        /// Gets the cached value using the constructor-provided fetcher.
        /// </summary>
        /// <returns>The cached or freshly fetched value.</returns>
        /// <exception cref="InvalidOperationException">Thrown when this cache was built with the
        /// parameterless constructor and so has no fetcher.</exception>
        public T Get()
        {
            if (_fetcher == null)
            {
                throw new InvalidOperationException("PerFrameCache was constructed without a fetcher. Use Get(Func<T>) or the PerFrameCache(Func<T>) constructor.");
            }

            int currentFrame = Time.frameCount;
            if (_cachedFrameCount != currentFrame)
            {
                _cachedValue = _fetcher();
                _cachedFrameCount = currentFrame;
                _hasValue = true;
            }
            return _cachedValue;
        }

        /// <summary>
        /// Gets the cached value, refreshing if this is a new frame.
        /// </summary>
        /// <param name="fetcher">Function to fetch the value if cache is invalid.</param>
        /// <returns>The cached or freshly fetched value.</returns>
        public T Get(Func<T> fetcher)
        {
            int currentFrame = Time.frameCount;
            if (_cachedFrameCount != currentFrame)
            {
                _cachedValue = fetcher();
                _cachedFrameCount = currentFrame;
                _hasValue = true;
            }
            return _cachedValue;
        }

        /// <summary>
        /// Whether the cache currently has a value.
        /// </summary>
        public bool HasValue
        {
            get { return _hasValue; }
        }

        /// <summary>
        /// Force invalidate the cache, causing the next Get to refresh.
        /// </summary>
        public void Invalidate()
        {
            _cachedFrameCount = -1;
            _hasValue = false;
        }

        /// <summary>
        /// Manually set the cached value.
        /// </summary>
        /// <param name="value">Value to cache.</param>
        public void Set(T value)
        {
            _cachedValue = value;
            _cachedFrameCount = Time.frameCount;
            _hasValue = true;
        }
    }
}
