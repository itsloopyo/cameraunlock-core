using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>One token of an <see cref="EnumCodec{TEnum}"/>: the word a file holds, and the enumerator it stands for.</summary>
    public struct EnumToken<TEnum> where TEnum : struct, Enum
    {
        /// <summary>A token and its enumerator.</summary>
        public EnumToken(string token, TEnum value)
        {
            Token = token;
            Value = value;
        }

        /// <summary>The word, in its declared spelling.</summary>
        public string Token { get; }

        /// <summary>The enumerator it reads as.</summary>
        public TEnum Value { get; }
    }
}
