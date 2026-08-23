
# Field Control Network Reference

This repository documents a minimal **Field Control Network (FCN)** pairing, composed of two Field Control Units (FCUs):

- **FCU-µ** – the embedded, field-facing node responsible for direct I/O and physical interaction.  
- **FCU-c (-py)** – the supervisory Python node responsible for coordination and interfacing with external systems.

The FCN defines a clear system boundary and role separation. Implementation details such as CPU architecture, operating system, or future interface layers are deliberately excluded from the naming conventions. Optional runtime rifts (e.g., `-py`, `-jvm`) indicate the language or runtime environment of an FCU.