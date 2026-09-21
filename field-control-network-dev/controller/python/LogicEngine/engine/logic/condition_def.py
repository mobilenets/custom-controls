from dataclasses import dataclass, field
from engine.runtime.enumerators import Tri
from typing import  Any
# ---------- Condition ----------
@dataclass
class ConditionDef:
    condition_id: str
    source_ref: str
    operator: str
    value: Any
    on_undefined: Tri = Tri.PASS