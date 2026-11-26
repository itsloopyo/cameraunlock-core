using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The rows a legacy import leaves to Defaults.ini (owner rule of 2026-09-26). A setting the
    /// player never changed from what the legacy build shipped, because the legacy file does not
    /// hold it or holds the shipped value, is no player's choice, so the migration writes it
    /// <c>default</c> and it follows Defaults.ini. The legacy build is the published build that
    /// wrote the file, an older one included, where the file shows which (owner ruling of
    /// 2026-09-27). A setting the player changed is carried as a
    /// value, written <c>default</c> only where it equals what <c>default</c> gives at that start.
    /// Every legacy import gives each row of its table that follows Defaults.ini (a global concept
    /// row not marked PerGame) one call, and passes <see cref="Concepts"/> to <see cref="ImportResult.Imported(IEnumerable{DroppedValue}, IEnumerable{PoseShapingValue}, IEnumerable{ConceptDescriptor})"/>
    /// or <c>Absent</c>. The map still sets every member from the legacy value. The C++ twin is
    /// <c>cameraunlock::config::LegacyFollowsDefaultsIni</c>.
    /// </summary>
    public sealed class LegacyFollowsDefaultsIni
    {
        private readonly List<ConceptDescriptor> _given = new List<ConceptDescriptor>();
        private readonly List<ConceptDescriptor> _concepts = new List<ConceptDescriptor>();

        /// <summary>The concepts left to Defaults.ini, in the order given.</summary>
        public ReadOnlyCollection<ConceptDescriptor> Concepts
        {
            get { return _concepts.AsReadOnly(); }
        }

        /// <summary>
        /// A setting the legacy build read: left to Defaults.ini when <paramref name="value"/>, the
        /// effective legacy value, equals <paramref name="shipped"/>, compared by
        /// <see cref="EqualityComparer{T}.Default"/>. <paramref name="shipped"/> is the effective
        /// value the published build that wrote the file shipped, which is not always the newest
        /// build's: where the default changed between published builds and the file shows which
        /// build wrote it, it is that build's (owner ruling of 2026-09-27). A float or double
        /// <paramref name="value"/> that is not finite is left to Defaults.ini too (N2, owner ruling
        /// of 2026-09-27): pass the value as the frozen reader read it, not what
        /// <see cref="LegacyNormalisations.FiniteOrDefault(float, float, string, string, ICollection{DroppedValue})"/> gave.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/> is null.</exception>
        /// <exception cref="ArgumentException">The concept is RotationEnabled or PositionEnabled
        /// (<see cref="TrackingMode(bool)"/>), is not global or was given before, or a float or double
        /// <paramref name="shipped"/> is not finite.</exception>
        public void Setting<T>(ConceptDescriptor concept, T value, T shipped)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            CheckShipped(concept, shipped);
            Setting(concept, !Finite(value) || EqualityComparer<T>.Default.Equals(value, shipped));
        }

        /// <summary>
        /// A setting whose default changed between published builds, where the file narrows the
        /// build that wrote it to several whose defaults differ: left to Defaults.ini when
        /// <paramref name="value"/> equals any of <paramref name="shipped"/>, the effective value
        /// each of those builds shipped, compared by <see cref="EqualityComparer{T}.Default"/>, or is
        /// a float or double that is not finite.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/> or
        /// <paramref name="shipped"/> is null.</exception>
        /// <exception cref="ArgumentException">As <see cref="Setting{T}(ConceptDescriptor, T, T)"/>,
        /// for any value of <paramref name="shipped"/>, or <paramref name="shipped"/> is empty.</exception>
        public void Setting<T>(ConceptDescriptor concept, T value, IList<T> shipped)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            if (shipped == null) throw new ArgumentNullException("shipped");
            if (shipped.Count == 0) throw new ArgumentException(concept.Key + ": no shipped value is given", "shipped");
            bool unchanged = !Finite(value);
            foreach (T one in shipped)
            {
                CheckShipped(concept, one);
                if (EqualityComparer<T>.Default.Equals(value, one)) unchanged = true;
            }
            Setting(concept, unchanged);
        }

        /// <summary>
        /// A setting the import compares itself, such as a hotkey together with its chord switch:
        /// left to Defaults.ini when <paramref name="unchanged"/>, which the import also sets for a
        /// number read that is not finite (N2).
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/> is null.</exception>
        /// <exception cref="ArgumentException">The concept is RotationEnabled or PositionEnabled
        /// (<see cref="TrackingMode(bool)"/>), or is not global or was given before.</exception>
        public void Setting(ConceptDescriptor concept, bool unchanged)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            if (concept == ConfigConcepts.RotationEnabled || concept == ConfigConcepts.PositionEnabled)
            {
                throw new ArgumentException(concept.Key + " is half of the tracking mode, which TrackingMode takes as one unit",
                    "concept");
            }
            if (!concept.Global)
            {
                throw new ArgumentException(concept.Key + " is not a global concept, so no row of it follows Defaults.ini",
                    "concept");
            }
            if (_given.Contains(concept)) throw new ArgumentException(concept.Key + " was given before", "concept");
            _given.Add(concept);
            if (unchanged) _concepts.Add(concept);
        }

        /// <summary>
        /// A concept the legacy build had no setting for, which no player can have changed: always
        /// left to Defaults.ini.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/> is null.</exception>
        /// <exception cref="ArgumentException">As <see cref="Setting(ConceptDescriptor, bool)"/>.</exception>
        public void NotInLegacy(ConceptDescriptor concept)
        {
            Setting(concept, true);
        }

        /// <summary>
        /// The tracking mode, RotationEnabled and PositionEnabled as one unit: both are left to
        /// Defaults.ini when <paramref name="value"/>, the legacy mode, equals
        /// <paramref name="shipped"/>, the mode the legacy build shipped.
        /// </summary>
        /// <exception cref="ArgumentException">The mode was given before.</exception>
        public void TrackingMode<T>(T value, T shipped)
        {
            TrackingMode(EqualityComparer<T>.Default.Equals(value, shipped));
        }

        /// <summary>
        /// The tracking mode, compared by the import over every legacy setting it derives the mode
        /// from, a position switch included: both rows are left to Defaults.ini when
        /// <paramref name="unchanged"/>.
        /// </summary>
        /// <exception cref="ArgumentException">The mode was given before.</exception>
        public void TrackingMode(bool unchanged)
        {
            if (_given.Contains(ConfigConcepts.PositionEnabled))
            {
                throw new ArgumentException("the tracking mode was given before", "unchanged");
            }
            _given.Add(ConfigConcepts.RotationEnabled);
            _given.Add(ConfigConcepts.PositionEnabled);
            if (!unchanged) return;
            _concepts.Add(ConfigConcepts.RotationEnabled);
            _concepts.Add(ConfigConcepts.PositionEnabled);
        }

        private static void CheckShipped<T>(ConceptDescriptor concept, T shipped)
        {
            if (!Finite(shipped)) throw new ArgumentException(concept.Key + ": the shipped value is not finite", "shipped");
        }

        private static bool Finite<T>(T value)
        {
            if (value is float f) return !float.IsNaN(f) && !float.IsInfinity(f);
            if (value is double d) return !double.IsNaN(d) && !double.IsInfinity(d);
            return true;
        }
    }
}
