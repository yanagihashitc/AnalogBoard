#!/usr/bin/env python3
"""Derive the D17 channel mapping from a pinned gcsa authority blob."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import stat
import subprocess
from collections.abc import Callable, Mapping, Sequence
from pathlib import Path, PurePosixPath
from typing import Any


_AUTHORITY_SYMBOLS = (
    "FL_CHANNEL_MAP",
    "FH_CHANNEL_MAP",
    "FL_CHANNEL_NAMES",
    "FH_CHANNEL_NAMES",
    "ALL_CHANNEL_NAMES",
)
_REQUIRED_SYMBOLS = _AUTHORITY_SYMBOLS
_FL_LABELS = ("FSC", "SSC", "FL1", "FL2", "FL3", "FL4", "FL5", "FL6")
_FH_LABELS = ("fsGMI", "ssGMI", "flGMI", "dGMI", "bfGMI")
_D17_LABELS = _FL_LABELS + _FH_LABELS
_FULL_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
_APPROVED_OUTPUT = PurePosixPath(
    "docs/reference/d17-golden-regression/channel-mapping-v1.json"
)
_DYNAMIC_BINDING_OPERATIONS = frozenset(
    {"globals", "locals", "vars", "exec", "eval", "setattr", "delattr"}
)


class MappingContractError(Exception):
    """Base class for stable, typed mapping-contract failures."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


class AuthoritySourceError(MappingContractError):
    """The authority source cannot be parsed by the restricted extractor."""


class MissingAuthoritySymbolError(MappingContractError):
    """A required authority symbol is absent."""


class AuthorityIndexError(MappingContractError):
    """A channel map contains an invalid source index."""


class AuthorityOrderError(MappingContractError):
    """The aggregate channel authority disagrees with map order."""


class AuthorityCommitError(MappingContractError):
    """The exact pinned authority blob cannot be read."""


class ContractOutputError(MappingContractError):
    """The requested contract output boundary is unsafe or unauthorized."""


class ContractDecisionRequiredError(MappingContractError):
    """The derived result disagrees with a frozen D17 decision."""


BlobReader = Callable[[Path, str, str], str]
ContractWriter = Callable[[Path, bytes], None]
SubprocessRunner = Callable[..., Any]


def _source_error(message: str) -> AuthoritySourceError:
    return AuthoritySourceError("authority.source.unsupported", message)


def _assignment_name(statement: ast.stmt) -> tuple[str, ast.expr] | None:
    if isinstance(statement, ast.Assign):
        if len(statement.targets) != 1 or not isinstance(statement.targets[0], ast.Name):
            return None
        return statement.targets[0].id, statement.value
    if (
        isinstance(statement, ast.AnnAssign)
        and isinstance(statement.target, ast.Name)
        and statement.value is not None
    ):
        return statement.target.id, statement.value
    return None


def _unsupported_authority_use(name: str) -> AuthoritySourceError:
    return _source_error(
        f"authority symbol has unsupported use, binding, or mutation: {name}"
    )


def _unsupported_dynamic_binding(operation: str) -> AuthoritySourceError:
    return _source_error(
        f"authority source has unsupported dynamic binding operation: {operation}"
    )


def _require_supported_authority_uses(
    tree: ast.Module,
    supported_targets: set[int],
    supported_loads: set[int],
) -> None:
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and node.id in _AUTHORITY_SYMBOLS:
            if (
                isinstance(node.ctx, (ast.Store, ast.Del))
                and id(node) not in supported_targets
            ):
                raise _unsupported_authority_use(node.id)
            if isinstance(node.ctx, ast.Load) and id(node) not in supported_loads:
                raise _unsupported_authority_use(node.id)

        if (
            isinstance(node, ast.Name)
            and isinstance(node.ctx, ast.Load)
            and node.id in _DYNAMIC_BINDING_OPERATIONS
        ):
            raise _unsupported_dynamic_binding(node.id)

        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "builtins"
            and node.func.attr in _DYNAMIC_BINDING_OPERATIONS
        ):
            raise _unsupported_dynamic_binding(node.func.attr)

        if isinstance(node, ast.Import):
            for alias in node.names:
                name = alias.asname or alias.name.partition(".")[0]
                if name in _AUTHORITY_SYMBOLS:
                    raise _unsupported_authority_use(name)

        if isinstance(node, ast.ImportFrom):
            for alias in node.names:
                name = alias.asname or alias.name
                if name in _AUTHORITY_SYMBOLS:
                    raise _unsupported_authority_use(name)

        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            if node.name in _AUTHORITY_SYMBOLS:
                raise _unsupported_authority_use(node.name)

        if isinstance(node, (ast.Global, ast.Nonlocal)):
            for name in node.names:
                if name in _AUTHORITY_SYMBOLS:
                    raise _unsupported_authority_use(name)

        if isinstance(node, ast.arg) and node.arg in _AUTHORITY_SYMBOLS:
            raise _unsupported_authority_use(node.arg)

        if (
            isinstance(node, ast.ExceptHandler)
            and node.name in _AUTHORITY_SYMBOLS
        ):
            raise _unsupported_authority_use(node.name)

        if isinstance(node, (ast.MatchAs, ast.MatchStar)):
            if node.name in _AUTHORITY_SYMBOLS:
                raise _unsupported_authority_use(node.name)

        if (
            isinstance(node, ast.MatchMapping)
            and node.rest in _AUTHORITY_SYMBOLS
        ):
            raise _unsupported_authority_use(node.rest)


def _collect_authority_assignments(tree: ast.Module) -> dict[str, ast.expr]:
    assignments: dict[str, ast.expr] = {}
    supported_targets: set[int] = set()
    supported_loads: set[int] = set()
    for statement in tree.body:
        assignment = _assignment_name(statement)
        if assignment is None:
            continue
        name, expression = assignment
        if name not in _AUTHORITY_SYMBOLS:
            continue
        if name in assignments:
            raise _source_error(f"authority symbol is assigned more than once: {name}")
        assignments[name] = expression
        if isinstance(statement, ast.Assign):
            supported_targets.add(id(statement.targets[0]))
        else:
            supported_targets.add(id(statement.target))
        supported_loads.update(
            id(node)
            for node in ast.walk(expression)
            if (
                isinstance(node, ast.Name)
                and isinstance(node.ctx, ast.Load)
                and node.id in _AUTHORITY_SYMBOLS
            )
        )
    _require_supported_authority_uses(
        tree,
        supported_targets,
        supported_loads,
    )
    return assignments


class _RestrictedAuthorityEvaluator:
    """Evaluate only the small expression subset used by channel authority."""

    def __init__(self, assignments: Mapping[str, ast.expr]) -> None:
        self._assignments = assignments
        self._values: dict[str, object] = {}
        self._active: set[str] = set()

    def resolve(self, name: str) -> object:
        if name in self._values:
            return self._values[name]
        if name not in self._assignments:
            raise MissingAuthoritySymbolError(
                "authority.symbol.missing",
                f"required authority symbol is missing: {name}",
            )
        if name in self._active:
            raise _source_error(f"cyclic authority expression for symbol: {name}")

        self._active.add(name)
        try:
            value = self._evaluate(self._assignments[name], owner=name)
        finally:
            self._active.remove(name)
        self._values[name] = value
        return value

    def _evaluate(self, node: ast.expr, *, owner: str) -> object:
        if isinstance(node, ast.Constant):
            if node.value is None or type(node.value) in (bool, int, str):
                return node.value
            raise _source_error(
                f"unsupported literal in authority symbol: {owner}"
            )

        if (
            isinstance(node, ast.UnaryOp)
            and isinstance(node.op, ast.USub)
            and isinstance(node.operand, ast.Constant)
            and type(node.operand.value) is int
        ):
            return -node.operand.value

        if isinstance(node, ast.Name):
            return self.resolve(node.id)

        if isinstance(node, ast.Dict):
            result: dict[object, object] = {}
            for key_node, value_node in zip(node.keys, node.values, strict=True):
                if key_node is None:
                    raise _source_error(
                        f"dictionary unpacking is not allowed in authority symbol: {owner}"
                    )
                key = self._evaluate(key_node, owner=owner)
                try:
                    if key in result:
                        raise _source_error(
                            f"duplicate dictionary key in authority symbol: {owner}"
                        )
                    result[key] = self._evaluate(value_node, owner=owner)
                except TypeError as exc:
                    raise _source_error(
                        f"unhashable dictionary key in authority symbol: {owner}"
                    ) from exc
            return result

        if isinstance(node, ast.Tuple):
            return tuple(self._evaluate(item, owner=owner) for item in node.elts)

        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = self._evaluate(node.left, owner=owner)
            right = self._evaluate(node.right, owner=owner)
            if type(left) is tuple and type(right) is tuple:
                return left + right
            raise _source_error(
                f"only tuple concatenation is allowed in authority symbol: {owner}"
            )

        if isinstance(node, ast.Call):
            return self._evaluate_tuple_keys_call(node, owner=owner)

        raise _source_error(
            f"unsupported expression in authority symbol: {owner}"
        )

    def _evaluate_tuple_keys_call(self, node: ast.Call, *, owner: str) -> tuple[object, ...]:
        if (
            not isinstance(node.func, ast.Name)
            or node.func.id != "tuple"
            or len(node.args) != 1
            or node.keywords
        ):
            raise _source_error(
                f"only tuple(MAP.keys()) calls are allowed in authority symbol: {owner}"
            )

        keys_call = node.args[0]
        if (
            not isinstance(keys_call, ast.Call)
            or keys_call.args
            or keys_call.keywords
            or not isinstance(keys_call.func, ast.Attribute)
            or keys_call.func.attr != "keys"
            or not isinstance(keys_call.func.value, ast.Name)
        ):
            raise _source_error(
                f"only tuple(MAP.keys()) calls are allowed in authority symbol: {owner}"
            )

        mapping = self.resolve(keys_call.func.value.id)
        if type(mapping) is not dict:
            raise _source_error(
                f"tuple keys source is not a dictionary in authority symbol: {owner}"
            )
        return tuple(mapping.keys())


def _require_symbols(assignments: Mapping[str, ast.expr]) -> None:
    missing = [name for name in _REQUIRED_SYMBOLS if name not in assignments]
    if not missing:
        return
    if len(missing) == 1:
        message = f"required authority symbol is missing: {missing[0]}"
    else:
        message = f"required authority symbols are missing: {', '.join(missing)}"
    raise MissingAuthoritySymbolError("authority.symbol.missing", message)


def _validate_channel_map(value: object, symbol: str) -> dict[str, int]:
    if type(value) is not dict:
        raise _source_error(f"{symbol} must be a dictionary")

    channel_map: dict[str, int] = {}
    seen_indices: set[int] = set()
    for label, source_index in value.items():
        if type(label) is not str:
            raise _source_error(f"{symbol} labels must be strings")
        if type(source_index) is not int:
            raise AuthorityIndexError(
                "authority.index.type",
                (
                    f"{symbol} index for {label} must be an integer; "
                    f"found {type(source_index).__name__}"
                ),
            )
        if source_index < 0:
            raise AuthorityIndexError(
                "authority.index.negative",
                (
                    f"{symbol} index for {label} must be non-negative; "
                    f"found {source_index}"
                ),
            )
        if source_index in seen_indices:
            raise AuthorityIndexError(
                "authority.index.duplicate",
                f"{symbol} indices must be unique; duplicate index {source_index}",
            )
        seen_indices.add(source_index)
        channel_map[label] = source_index

    sorted_indices = sorted(seen_indices)
    if sorted_indices != list(range(len(channel_map))):
        raise AuthorityIndexError(
            "authority.index.gap",
            (
                f"{symbol} indices must be contiguous from 0; "
                f"found {sorted_indices}"
            ),
        )
    if list(channel_map.values()) != list(range(len(channel_map))):
        raise AuthorityIndexError(
            "authority.index.order",
            f"{symbol} key order must match its contiguous source indices",
        )
    return channel_map


def _validate_channel_names(
    value: object,
    symbol: str,
    channel_map: Mapping[str, int],
    channel_map_symbol: str,
) -> tuple[str, ...]:
    if type(value) is not tuple:
        raise _source_error(f"{symbol} must be a tuple")
    expected = tuple(channel_map)
    if value != expected:
        raise AuthorityOrderError(
            "authority.order.mismatch",
            f"{symbol} order does not match {channel_map_symbol}",
        )
    return value


def _require_d17_labels(fl_labels: Sequence[str], fh_labels: Sequence[str]) -> None:
    combined = tuple(fl_labels) + tuple(fh_labels)
    if len(combined) != 13:
        raise ContractDecisionRequiredError(
            "mapping.channel_count.decision_required",
            f"derived mapping must contain exactly 13 channels; found {len(combined)}",
        )

    actual = set(combined)
    expected = set(_D17_LABELS)
    if actual != expected or len(actual) != len(combined):
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ContractDecisionRequiredError(
            "mapping.labels.decision_required",
            (
                "derived labels do not match the frozen D17 label set; "
                f"missing={missing}; extra={extra}"
            ),
        )

    if tuple(fl_labels) != _FL_LABELS or tuple(fh_labels) != _FH_LABELS:
        raise ContractDecisionRequiredError(
            "mapping.stream_labels.decision_required",
            "derived FL/FH channel groups do not match the frozen D17 stream mapping",
        )


def derive_mapping_from_source(source: object) -> list[dict[str, object]]:
    """Derive CH1-CH13 without executing or importing the authority module."""

    if type(source) is not str:
        raise AuthoritySourceError(
            "authority.source.type",
            "authority source must be a string",
        )
    try:
        tree = ast.parse(source)
    except (SyntaxError, ValueError) as exc:
        raise AuthoritySourceError(
            "authority.source.syntax",
            "authority source is not valid Python syntax",
        ) from exc

    assignments = _collect_authority_assignments(tree)
    _require_symbols(assignments)
    evaluator = _RestrictedAuthorityEvaluator(assignments)
    fl_map = _validate_channel_map(
        evaluator.resolve("FL_CHANNEL_MAP"), "FL_CHANNEL_MAP"
    )
    fh_map = _validate_channel_map(
        evaluator.resolve("FH_CHANNEL_MAP"), "FH_CHANNEL_MAP"
    )
    fl_names = _validate_channel_names(
        evaluator.resolve("FL_CHANNEL_NAMES"),
        "FL_CHANNEL_NAMES",
        fl_map,
        "FL_CHANNEL_MAP",
    )
    fh_names = _validate_channel_names(
        evaluator.resolve("FH_CHANNEL_NAMES"),
        "FH_CHANNEL_NAMES",
        fh_map,
        "FH_CHANNEL_MAP",
    )
    fl_labels = tuple(fl_map)
    fh_labels = tuple(fh_map)
    _require_d17_labels(fl_labels, fh_labels)

    all_names = evaluator.resolve("ALL_CHANNEL_NAMES")
    if type(all_names) is not tuple:
        raise _source_error("ALL_CHANNEL_NAMES must be a tuple")
    if all_names != fl_names + fh_names:
        raise AuthorityOrderError(
            "authority.order.mismatch",
            "ALL_CHANNEL_NAMES order does not match FL_CHANNEL_MAP + FH_CHANNEL_MAP",
        )

    mapping: list[dict[str, object]] = []
    position = 1
    for stream, channel_map in (("FL", fl_map), ("FH", fh_map)):
        for label, source_index in channel_map.items():
            mapping.append(
                {
                    "physical_channel": f"CH{position}",
                    "stream": stream,
                    "source_index": source_index,
                    "label": label,
                }
            )
            position += 1
    return mapping


def _normalize_source_path(source_path: object) -> str:
    if type(source_path) is not str:
        raise AuthoritySourceError(
            "authority.path.type",
            "authority source path must be a string",
        )
    candidate = PurePosixPath(source_path)
    if (
        not source_path
        or candidate.is_absolute()
        or ".." in candidate.parts
        or "\\" in source_path
        or candidate.as_posix() != source_path
    ):
        raise AuthoritySourceError(
            "authority.path.invalid",
            "authority source path must be a normalized repository-relative path",
        )
    return source_path


def read_git_blob(
    repository: Path,
    commit: str,
    source_path: str,
    *,
    run: SubprocessRunner = subprocess.run,
) -> str:
    """Read an exact Git blob with replacement objects disabled."""

    completed = run(
        [
            "git",
            "--no-replace-objects",
            "-C",
            str(repository),
            "--no-pager",
            "show",
            f"{commit}:{source_path}",
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
    )
    return completed.stdout


def _read_pinned_source(
    repository: Path,
    commit: object,
    source_path: object,
    read_blob: BlobReader,
) -> str:
    normalized_path = _normalize_source_path(source_path)
    if type(commit) is not str or _FULL_COMMIT_RE.fullmatch(commit) is None:
        raise AuthorityCommitError(
            "authority.commit.invalid",
            "authority commit must be a full lowercase hexadecimal Git object ID",
        )
    try:
        return read_blob(repository, commit, normalized_path)
    except (OSError, subprocess.SubprocessError, UnicodeError) as exc:
        raise AuthorityCommitError(
            "authority.commit.unreadable",
            f"unable to read pinned authority blob {commit}:{normalized_path}",
        ) from exc


def derive_mapping_from_git(
    repository: Path,
    commit: object,
    source_path: object,
    *,
    read_blob: BlobReader = read_git_blob,
) -> list[dict[str, object]]:
    """Read one exact commit:path blob and derive its mapping without fallback."""

    source = _read_pinned_source(repository, commit, source_path, read_blob)
    return derive_mapping_from_source(source)


def serialize_mapping_contract(
    mapping: Sequence[Mapping[str, object]],
    provenance: Mapping[str, object],
) -> bytes:
    """Serialize a payload-free mapping contract as canonical JSON plus one LF."""

    contract = {
        "schema": "analogboard.d17.channel-mapping",
        "schema_version": 1,
        "channel_count": len(mapping),
        "mapping": [dict(entry) for entry in mapping],
        "provenance": dict(provenance),
    }
    return (
        json.dumps(
            contract,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _validated_contract_output(
    repository_root: Path,
    output_path: Path,
) -> Path:
    if (
        not isinstance(output_path, Path)
        or output_path.is_absolute()
        or PurePosixPath(output_path.as_posix()) != _APPROVED_OUTPUT
        or output_path.as_posix() != _APPROVED_OUTPUT.as_posix()
    ):
        raise ContractOutputError(
            "contract.output.path.invalid",
            f"contract output must be {_APPROVED_OUTPUT.as_posix()}",
        )

    try:
        resolved_root = repository_root.resolve(strict=True)
        root_mode = repository_root.lstat().st_mode
    except OSError as exc:
        raise ContractOutputError(
            "contract.output.root.invalid",
            "repository root must be an existing canonical directory",
        ) from exc
    if (
        not repository_root.is_absolute()
        or repository_root != resolved_root
        or not stat.S_ISDIR(root_mode)
        or stat.S_ISLNK(root_mode)
    ):
        raise ContractOutputError(
            "contract.output.root.invalid",
            "repository root must be an existing canonical directory",
        )

    current = repository_root
    for component in _APPROVED_OUTPUT.parent.parts:
        current = current / component
        try:
            mode = current.lstat().st_mode
        except OSError as exc:
            raise ContractOutputError(
                "contract.output.parent.unsafe",
                "approved contract output parent must contain only directories",
            ) from exc
        if not stat.S_ISDIR(mode) or stat.S_ISLNK(mode):
            raise ContractOutputError(
                "contract.output.parent.unsafe",
                "approved contract output parent must contain only directories",
            )

    approved_output = repository_root / Path(_APPROVED_OUTPUT.as_posix())
    try:
        target_mode = approved_output.lstat().st_mode
    except FileNotFoundError:
        return approved_output
    except OSError as exc:
        raise ContractOutputError(
            "contract.output.target.unsafe",
            "approved contract output must be absent or a regular file",
        ) from exc
    if not stat.S_ISREG(target_mode) or stat.S_ISLNK(target_mode):
        raise ContractOutputError(
            "contract.output.target.unsafe",
            "approved contract output must be absent or a regular file",
        )
    return approved_output


def _contract_directory_flags() -> int:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if (
        no_follow == 0
        or directory == 0
        or os.open not in os.supports_dir_fd
        or os.stat not in os.supports_dir_fd
        or os.stat not in os.supports_follow_symlinks
    ):
        raise ContractOutputError(
            "contract.output.parent.unsafe",
            "approved contract output parent cannot be opened safely",
        )
    return (
        os.O_RDONLY
        | directory
        | no_follow
        | getattr(os, "O_CLOEXEC", 0)
    )


def _open_contract_output_parent(repository_root: Path) -> int:
    flags = _contract_directory_flags()
    opened: int | None = None
    try:
        directory = os.open(repository_root, flags)
        for component in _APPROVED_OUTPUT.parent.parts:
            before = os.stat(
                component,
                dir_fd=directory,
                follow_symlinks=False,
            )
            if not stat.S_ISDIR(before.st_mode) or stat.S_ISLNK(before.st_mode):
                raise OSError("unsafe output parent")
            opened = os.open(component, flags, dir_fd=directory)
            after = os.fstat(opened)
            if (
                not stat.S_ISDIR(after.st_mode)
                or (before.st_dev, before.st_ino)
                != (after.st_dev, after.st_ino)
            ):
                os.close(opened)
                opened = None
                raise OSError("output parent identity changed")
            os.close(directory)
            directory = opened
            opened = None
        return directory
    except (OSError, ValueError) as exc:
        if opened is not None:
            os.close(opened)
        if "directory" in locals():
            os.close(directory)
        raise ContractOutputError(
            "contract.output.parent.unsafe",
            "approved contract output parent cannot be opened safely",
        ) from exc


def _require_current_contract_parent(
    repository_root: Path,
    expected_descriptor: int,
) -> None:
    try:
        current = _open_contract_output_parent(repository_root)
    except ContractOutputError as exc:
        raise ContractOutputError(
            "contract.output.parent.changed",
            "approved contract output parent changed during generation",
        ) from exc
    try:
        expected = os.fstat(expected_descriptor)
        actual = os.fstat(current)
        if (expected.st_dev, expected.st_ino) != (actual.st_dev, actual.st_ino):
            raise ContractOutputError(
                "contract.output.parent.changed",
                "approved contract output parent changed during generation",
            )
    finally:
        os.close(current)


def _read_existing_contract(parent: int) -> bytes | None:
    filename = _APPROVED_OUTPUT.name
    try:
        metadata = os.stat(filename, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise ContractOutputError(
            "contract.output.read",
            "unable to read existing approved contract output",
        ) from exc
    if (
        not stat.S_ISREG(metadata.st_mode)
        or stat.S_ISLNK(metadata.st_mode)
        or metadata.st_nlink != 1
    ):
        raise ContractOutputError(
            "contract.output.target.unsafe",
            "approved contract output must be absent or a regular unaliased file",
        )
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_CLOEXEC", 0)
    try:
        descriptor = os.open(filename, flags, dir_fd=parent)
        opened = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_nlink != 1
            or (metadata.st_dev, metadata.st_ino)
            != (opened.st_dev, opened.st_ino)
        ):
            raise OSError("contract output identity changed")
        chunks: list[bytes] = []
        while True:
            chunk = os.read(descriptor, 64 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
        after = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
            or after.st_nlink != 1
        ):
            raise OSError("contract output changed during read")
        return b"".join(chunks)
    except OSError as exc:
        raise ContractOutputError(
            "contract.output.read",
            "unable to read existing approved contract output",
        ) from exc
    finally:
        if "descriptor" in locals():
            os.close(descriptor)


def _write_contract_at(parent: int, content: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_CLOEXEC", 0)
    try:
        descriptor = os.open(
            _APPROVED_OUTPUT.name,
            flags,
            0o644,
            dir_fd=parent,
        )
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
            raise OSError("unsafe created contract output")
        offset = 0
        while offset < len(content):
            written = os.write(descriptor, content[offset:])
            if written <= 0:
                raise OSError("short contract write")
            offset += written
    except OSError as exc:
        raise ContractOutputError(
            "contract.output.write",
            "unable to write approved contract output",
        ) from exc
    finally:
        if "descriptor" in locals():
            os.close(descriptor)


def generate_mapping_contract(
    *,
    repository_root: Path,
    output_path: Path,
    gcsa_repository: Path,
    commit: object,
    source_path: object,
    read_blob: BlobReader = read_git_blob,
    write_bytes: ContractWriter | None = None,
) -> Path:
    """Generate the one approved tracked mapping contract."""

    approved_output = _validated_contract_output(repository_root, output_path)
    parent = _open_contract_output_parent(repository_root)
    try:
        source = _read_pinned_source(
            gcsa_repository,
            commit,
            source_path,
            read_blob,
        )
        mapping = derive_mapping_from_source(source)
        provenance = {
            "repository": "gcsa",
            "commit": commit,
            "path": source_path,
            "source_sha256": hashlib.sha256(source.encode("utf-8")).hexdigest(),
            "symbols": list(_AUTHORITY_SYMBOLS),
        }
        content = serialize_mapping_contract(mapping, provenance)
        _require_current_contract_parent(repository_root, parent)
        existing_content = _read_existing_contract(parent)
        if existing_content is not None:
            if existing_content != content:
                raise ContractOutputError(
                    "contract.output.mismatch",
                    "existing contract output differs from generated content",
                )
            return approved_output
        if write_bytes is None:
            _write_contract_at(parent, content)
        else:
            try:
                write_bytes(approved_output, content)
            except OSError as exc:
                raise ContractOutputError(
                    "contract.output.write",
                    "unable to write approved contract output",
                ) from exc
        _require_current_contract_parent(repository_root, parent)
        return approved_output
    finally:
        os.close(parent)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate the D17 channel mapping from a pinned gcsa blob."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("--gcsa-repo", required=True, type=Path)
    generate.add_argument("--commit", required=True)
    generate.add_argument("--source-path", required=True)
    generate.add_argument("--output", required=True, type=Path)
    return parser


def _production_repository_root() -> Path:
    """Return the canonical checkout containing this tracked generator."""

    try:
        return Path(__file__).resolve(strict=True).parents[2]
    except (OSError, IndexError) as exc:
        raise ContractOutputError(
            "contract.output.root.invalid",
            "unable to locate the AnalogBoard repository root",
        ) from exc


def _generate(args: argparse.Namespace) -> None:
    generate_mapping_contract(
        repository_root=_production_repository_root(),
        output_path=args.output,
        gcsa_repository=args.gcsa_repo,
        commit=args.commit,
        source_path=args.source_path,
    )


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()
    try:
        if args.command == "generate":
            _generate(args)
    except MappingContractError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
