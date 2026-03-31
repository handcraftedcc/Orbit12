"""Condition parsing/evaluation for Orbit12 UI visibility."""

OPS = ("<=", ">=", "==", "!=", "<", ">")


def _parse_literal(token, context):
    token = token.strip()
    if not token:
        return ""

    if (token[0] == '"' and token[-1] == '"') or (token[0] == "'" and token[-1] == "'"):
        return token[1:-1]

    lower = token.lower()
    if lower == "true":
        return True
    if lower == "false":
        return False

    try:
        if "." in token:
            return float(token)
        return int(token)
    except ValueError:
        pass

    if token in context:
        return context[token]
    return token


def evaluate_condition(expression, context):
    """Evaluate a simple visibility condition expression.

    Supported operators: ==, !=, <, >, <=, >=
    """
    if not expression:
        return True

    text = expression.strip()
    for op in OPS:
        idx = text.find(op)
        if idx == -1:
            continue

        left_key = text[:idx].strip()
        right_token = text[idx + len(op) :].strip()
        if left_key not in context:
            return False

        left = context[left_key]
        right = _parse_literal(right_token, context)

        if op == "==":
            return left == right
        if op == "!=":
            return left != right

        if not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
            return False

        if op == "<":
            return left < right
        if op == ">":
            return left > right
        if op == "<=":
            return left <= right
        if op == ">=":
            return left >= right

    return False
