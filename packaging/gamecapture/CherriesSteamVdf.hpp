/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QByteArray>
#include <QStringList>
#include <vector>
#include <stdexcept>

namespace CherriesCapture {
// Keep byte ranges so changing one setting never reserializes Steam's other data.
struct VdfNode {
	QByteArray key, value;
	qsizetype begin = 0, end = 0, valueBegin = 0, valueEnd = 0, close = -1;
	bool object = false;
	std::vector<VdfNode> children;
	const VdfNode *child(const QByteArray &name) const
	{
		const VdfNode *found = nullptr;
		for (const auto &item : children) {
			if (item.key.compare(name, Qt::CaseInsensitive) != 0)
				continue;
			if (found)
				throw std::runtime_error("Ambiguous duplicate Steam setting; no changes were made.");
			found = &item;
		}
		return found;
	}
};

class VdfDocument {
	qsizetype position = 0;
	void whitespace()
	{
		while (position < bytes.size()) {
			char c = bytes[position];
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				position++;
			} else if (bytes.mid(position, 2) == "//") {
				while (position < bytes.size() && bytes[position] != '\n')
					position++;
			} else {
				break;
			}
		}
	}
	QByteArray token()
	{
		whitespace();
		QByteArray result;
		if (position >= bytes.size())
			throw std::runtime_error("Incomplete Steam settings file.");
		if (bytes[position] != '"') {
			qsizetype start = position;
			while (position < bytes.size() && !QByteArray(" \t\r\n{}[]").contains(bytes[position]))
				position++;
			if (start == position)
				throw std::runtime_error("Unsupported Steam settings syntax.");
			return bytes.mid(start, position - start);
		}
		position++;
		while (position < bytes.size()) {
			char c = bytes[position++];
			if (c == '"')
				return result;
			if (c == '\\') {
				if (position == bytes.size())
					break;
				char escaped = bytes[position++];
				switch (escaped) {
				case 'n':
					result += '\n';
					break;
				case 'r':
					result += '\r';
					break;
				case 't':
					result += '\t';
					break;
				case '\\':
				case '"':
					result += escaped;
					break;
				default:
					result += '\\';
					result += escaped;
					break;
				}
			} else {
				result += c;
			}
		}
		throw std::runtime_error("Unterminated Steam settings string.");
	}
	void object(VdfNode &parent, int depth)
	{
		if (depth > 64)
			throw std::runtime_error("Steam settings nesting is too deep.");
		for (;;) {
			whitespace();
			if (position == bytes.size()) {
				if (depth)
					throw std::runtime_error("Unclosed Steam settings object.");
				return;
			}
			if (bytes[position] == '}') {
				if (!depth)
					throw std::runtime_error("Unexpected Steam settings brace.");
				parent.close = position++;
				return;
			}
			VdfNode node;
			node.begin = position;
			node.key = token();
			whitespace();
			if (position < bytes.size() && bytes[position] == '{') {
				node.object = true;
				position++;
				object(node, depth + 1);
			} else {
				node.valueBegin = position;
				node.value = token();
				node.valueEnd = position;
			}
			node.end = position;
			parent.children.push_back(std::move(node));
		}
	}

public:
	QByteArray bytes;
	VdfNode root;
	explicit VdfDocument(QByteArray data) : bytes(std::move(data))
	{
		if (bytes.size() > 32 * 1024 * 1024 || bytes.contains('\0'))
			throw std::runtime_error("Invalid or oversized Steam settings file.");
		if (bytes.startsWith("\xef\xbb\xbf"))
			position = 3;
		root.object = true;
		object(root, 0);
	}
	static QByteArray quote(const QByteArray &value)
	{
		QByteArray result = "\"";
		for (char c : value) {
			switch (c) {
			case '\\':
			case '"':
				result += '\\';
				result += c;
				break;
			case '\n':
				result += "\\n";
				break;
			case '\r':
				result += "\\r";
				break;
			case '\t':
				result += "\\t";
				break;
			default:
				result += c;
				break;
			}
		}
		return result + '"';
	}
	const VdfNode *find(const QStringList &path) const
	{
		const VdfNode *node = &root;
		for (const auto &part : path) {
			if (!node->object)
				throw std::runtime_error("Unexpected Steam settings value in object path.");
			node = node->child(part.toUtf8());
			if (!node)
				return nullptr;
		}
		return node;
	}
	QByteArray set(const QStringList &path, const QByteArray &value, bool remove = false) const
	{
		if (path.isEmpty())
			throw std::runtime_error("Empty Steam settings path.");
		const VdfNode *parent = &root;
		for (qsizetype i = 0; i < path.size(); i++) {
			if (!parent->object)
				throw std::runtime_error("Unexpected Steam settings value in object path.");
			const auto *node = parent->child(path[i].toUtf8());
			if (node) {
				if (i == path.size() - 1) {
					if (node->object)
						throw std::runtime_error("Launch options must be a string.");
					QByteArray result = bytes;
					if (remove)
						result.remove(node->begin, node->end - node->begin);
					else
						result.replace(node->valueBegin, node->valueEnd - node->valueBegin,
							       quote(value));
					return result;
				}
				parent = node;
				continue;
			}
			if (remove)
				return bytes;
			const QByteArray newline = bytes.contains("\r\n") ? "\r\n" : "\n";
			QByteArray insertion = quote(path.last().toUtf8()) + "\t\t" + quote(value) + newline;
			for (qsizetype j = path.size() - 2; j >= i; j--)
				insertion =
					quote(path[j].toUtf8()) + newline + "{" + newline + insertion + "}" + newline;
			QByteArray result = bytes;
			result.insert(parent->close >= 0 ? parent->close : bytes.size(), newline + insertion);
			return result;
		}
		return bytes;
	}
};
} // namespace CherriesCapture
