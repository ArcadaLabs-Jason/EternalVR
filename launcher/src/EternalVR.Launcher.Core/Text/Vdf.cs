using System;
using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Text
{
    /// <summary>A node of Valve's text KeyValues format (libraryfolders.vdf, appmanifest_*.acf).</summary>
    public sealed class VdfNode
    {
        private readonly List<KeyValuePair<string, VdfNode>> children = new List<KeyValuePair<string, VdfNode>>();

        public VdfNode(string value = null) { Value = value; }

        /// <summary>The string value of a leaf; null for a block.</summary>
        public string Value { get; }

        public IReadOnlyList<KeyValuePair<string, VdfNode>> Children => children;

        internal void Add(string key, VdfNode node) => children.Add(new KeyValuePair<string, VdfNode>(key, node));

        /// <summary>First child with the key, compared case-insensitively as Steam does; null if absent.</summary>
        public VdfNode this[string key]
        {
            get
            {
                foreach (var child in children)
                    if (string.Equals(child.Key, key, StringComparison.OrdinalIgnoreCase))
                        return child.Value;
                return null;
            }
        }

        /// <summary>Follows a path of keys; null as soon as one is missing.</summary>
        public VdfNode Path(params string[] keys)
        {
            VdfNode node = this;
            foreach (var key in keys)
            {
                node = node?[key];
                if (node == null) return null;
            }
            return node;
        }

        public string GetString(string key) => this[key]?.Value;
    }

    public sealed class VdfFormatException : Exception
    {
        public VdfFormatException(string message) : base(message) { }
    }

    /// <summary>
    /// Parser for the text KeyValues format: quoted or bare tokens, nested blocks, // comments and the
    /// escape sequences Steam writes (\\, \", \n, \t). Conditional suffixes like [$WIN32] are skipped.
    /// </summary>
    public static class VdfParser
    {
        public static VdfNode Parse(string text)
        {
            var reader = new Reader(text ?? string.Empty);
            var root = new VdfNode();
            ParseBlock(reader, root, topLevel: true);
            return root;
        }

        private static void ParseBlock(Reader reader, VdfNode block, bool topLevel)
        {
            while (true)
            {
                var key = reader.Next();
                if (key == null)
                {
                    if (topLevel) return;
                    throw new VdfFormatException("unexpected end of file inside a block");
                }
                if (key.Kind == TokenKind.Close)
                {
                    if (topLevel) throw new VdfFormatException("unmatched '}'");
                    return;
                }
                if (key.Kind == TokenKind.Open) throw new VdfFormatException("'{' where a key was expected");

                var value = reader.Next();
                if (value == null) throw new VdfFormatException($"key '{key.Text}' has no value");
                if (value.Kind == TokenKind.Open)
                {
                    var child = new VdfNode();
                    ParseBlock(reader, child, topLevel: false);
                    block.Add(key.Text, child);
                }
                else if (value.Kind == TokenKind.String)
                {
                    block.Add(key.Text, new VdfNode(value.Text));
                }
                else
                {
                    throw new VdfFormatException($"key '{key.Text}' is followed by '}}'");
                }
            }
        }

        private enum TokenKind { String, Open, Close }

        private sealed class Token
        {
            public Token(TokenKind kind, string text) { Kind = kind; Text = text; }
            public TokenKind Kind { get; }
            public string Text { get; }
        }

        private sealed class Reader
        {
            private readonly string s;
            private int i;

            public Reader(string text) { s = text; }

            public Token Next()
            {
                while (true)
                {
                    SkipSpaceAndComments();
                    if (i >= s.Length) return null;
                    char c = s[i];
                    if (c == '{') { i++; return new Token(TokenKind.Open, "{"); }
                    if (c == '}') { i++; return new Token(TokenKind.Close, "}"); }
                    if (c == '[')
                    {
                        // Conditional such as [$WIN32]: skip it; it never carries data we need.
                        int end = s.IndexOf(']', i);
                        i = end < 0 ? s.Length : end + 1;
                        continue;
                    }
                    if (c == '"') return new Token(TokenKind.String, ReadQuoted());
                    return new Token(TokenKind.String, ReadBare());
                }
            }

            private void SkipSpaceAndComments()
            {
                while (i < s.Length)
                {
                    if (char.IsWhiteSpace(s[i]) || s[i] == '﻿') { i++; continue; }
                    if (s[i] == '/' && i + 1 < s.Length && s[i + 1] == '/')
                    {
                        while (i < s.Length && s[i] != '\n') i++;
                        continue;
                    }
                    break;
                }
            }

            private string ReadQuoted()
            {
                i++; // opening quote
                var sb = new StringBuilder();
                while (i < s.Length)
                {
                    char c = s[i++];
                    if (c == '"') return sb.ToString();
                    if (c == '\\' && i < s.Length)
                    {
                        char e = s[i++];
                        switch (e)
                        {
                            case 'n': sb.Append('\n'); break;
                            case 't': sb.Append('\t'); break;
                            case '\\': sb.Append('\\'); break;
                            case '"': sb.Append('"'); break;
                            default: sb.Append('\\').Append(e); break;
                        }
                        continue;
                    }
                    sb.Append(c);
                }
                throw new VdfFormatException("unterminated quoted string");
            }

            private string ReadBare()
            {
                int start = i;
                while (i < s.Length && !char.IsWhiteSpace(s[i]) && s[i] != '{' && s[i] != '}' && s[i] != '"')
                    i++;
                return s.Substring(start, i - start);
            }
        }
    }
}
