using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EternalVR.Launcher.Core.Text
{
    /// <summary>
    /// A small JSON reader for layer and runtime manifests. Objects become
    /// <c>Dictionary&lt;string, object&gt;</c> (case-sensitive keys), arrays <c>List&lt;object&gt;</c>,
    /// numbers <c>double</c>, plus string, bool and null. Anything malformed (a number out of range, nesting deeper than
    /// <see cref="MostDepth"/> included) throws <see cref="FormatException"/>, never anything else.
    /// </summary>
    public static class MiniJson
    {
        /// <summary>The deepest nesting of objects and arrays read; deeper throws (a manifest of "[[[[..." would else
        /// overflow the stack, which nothing can catch).</summary>
        public const int MostDepth = 64;

        public static object Parse(string text)
        {
            int i = 0;
            var s = text ?? string.Empty;
            if (s.Length > 0 && s[0] == '﻿') i = 1;
            var value = ReadValue(s, ref i, 0);
            SkipWs(s, ref i);
            if (i != s.Length) throw new FormatException("trailing characters at " + i);
            return value;
        }

        /// <summary>Follows object keys; null if any step is missing or not an object.</summary>
        public static object Get(object node, params string[] path)
        {
            foreach (var key in path)
            {
                if (!(node is Dictionary<string, object> obj) || !obj.TryGetValue(key, out node)) return null;
            }
            return node;
        }

        private static object ReadValue(string s, ref int i, int depth)
        {
            SkipWs(s, ref i);
            if (i >= s.Length) throw new FormatException("unexpected end");
            char c = s[i];
            if ((c == '{' || c == '[') && depth >= MostDepth) throw new FormatException("nested deeper than " + MostDepth + " at " + i);
            if (c == '{') return ReadObject(s, ref i, depth + 1);
            if (c == '[') return ReadArray(s, ref i, depth + 1);
            if (c == '"') return ReadString(s, ref i);
            if (Match(s, ref i, "true")) return true;
            if (Match(s, ref i, "false")) return false;
            if (Match(s, ref i, "null")) return null;
            return ReadNumber(s, ref i);
        }

        private static Dictionary<string, object> ReadObject(string s, ref int i, int depth)
        {
            var obj = new Dictionary<string, object>(StringComparer.Ordinal);
            i++;
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == '}') { i++; return obj; }
            while (true)
            {
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != '"') throw new FormatException("expected a key at " + i);
                var key = ReadString(s, ref i);
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException("expected ':' at " + i);
                i++;
                obj[key] = ReadValue(s, ref i, depth);
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == '}') { i++; return obj; }
                throw new FormatException("expected ',' or '}' at " + i);
            }
        }

        private static List<object> ReadArray(string s, ref int i, int depth)
        {
            var list = new List<object>();
            i++;
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == ']') { i++; return list; }
            while (true)
            {
                list.Add(ReadValue(s, ref i, depth));
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == ']') { i++; return list; }
                throw new FormatException("expected ',' or ']' at " + i);
            }
        }

        private static string ReadString(string s, ref int i)
        {
            i++;
            var sb = new StringBuilder();
            while (i < s.Length)
            {
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c != '\\') { sb.Append(c); continue; }
                if (i >= s.Length) break;
                char e = s[i++];
                switch (e)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 > s.Length) throw new FormatException("bad \\u escape");
                        sb.Append((char)int.Parse(s.Substring(i, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                        i += 4;
                        break;
                    default: throw new FormatException("bad escape \\" + e);
                }
            }
            throw new FormatException("unterminated string");
        }

        private static double ReadNumber(string s, ref int i)
        {
            int start = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            if (start == i) throw new FormatException("unexpected character '" + s[i] + "' at " + i);
            return Number(s.Substring(start, i - start), t => double.Parse(t, NumberStyles.Float, CultureInfo.InvariantCulture));
        }

        /// <summary>
        /// A number token through <paramref name="parse"/>: out of range is a <see cref="FormatException"/> like any other
        /// bad number, whether the framework throws <see cref="OverflowException"/> (.NET Framework 4.8, the launcher's) or
        /// returns infinity (.NET Core 3.0 and later).
        /// </summary>
        internal static double Number(string token, Func<string, double> parse)
        {
            double value;
            try { value = parse(token); }
            catch (OverflowException) { throw new FormatException("number out of range: " + token); }
            if (double.IsInfinity(value) || double.IsNaN(value)) throw new FormatException("number out of range: " + token);
            return value;
        }

        private static bool Match(string s, ref int i, string word)
        {
            if (string.CompareOrdinal(s, i, word, 0, word.Length) != 0) return false;
            i += word.Length;
            return true;
        }

        private static void SkipWs(string s, ref int i)
        {
            while (i < s.Length && char.IsWhiteSpace(s[i])) i++;
        }
    }
}
