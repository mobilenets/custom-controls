from dataclasses import dataclass
from typing import List

@dataclass
class GroupDef:
    group_id: str
    members: List[str]
    aggregation: str  # any | all | pass
