using System;

namespace SinCoreLib
{
    public class BitFlag_Util
    {
        private static void ValidateEnumType<T>() where T : Enum
        {
            if (!typeof(T).IsDefined(typeof(FlagsAttribute), false))
            {
                throw new ArgumentException($"{typeof(T).Name} must have the [Flags] attribute.");
            }
        }

        public static bool IsFlagSet<T>(T value, T flag) where T : Enum
        {
            ValidateEnumType<T>();
            var intValue = Convert.ToInt32(value);
            var intFlag = Convert.ToInt32(flag);
            return (intValue & intFlag) == intFlag;
        }

        public static T SetFlag<T>(T value, T flag, bool setValue) where T : Enum
        {
            if (setValue) return SetFlag(value, flag);
            else return ClearFlag(value, flag);
        }

        public static T SetFlag<T>(T value, T flag) where T : Enum
        {
            ValidateEnumType<T>();
            var intValue = Convert.ToInt32(value);
            var intFlag = Convert.ToInt32(flag);
            return (T)Enum.ToObject(typeof(T), intValue | intFlag);
        }

        public static T ClearFlag<T>(T value, T flag) where T : Enum
        {
            ValidateEnumType<T>();
            var intValue = Convert.ToInt32(value);
            var intFlag = Convert.ToInt32(flag);
            return (T)Enum.ToObject(typeof(T), intValue & ~intFlag);
        }

        public static T ToggleFlag<T>(T value, T flag) where T : Enum
        {
            ValidateEnumType<T>();
            var intValue = Convert.ToInt32(value);
            var intFlag = Convert.ToInt32(flag);
            return (T)Enum.ToObject(typeof(T), intValue ^ intFlag);
        }
    }
}
