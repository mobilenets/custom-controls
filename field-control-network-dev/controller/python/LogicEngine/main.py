import time
from engine.runtime_engine import LogicEngine
from engine.config.xml_loader import load_logic_config


def main():
    nodes, groups, logic_records, node_map = load_logic_config(
        "config/logic_config.xml"
    )

    engine = LogicEngine(nodes, groups, logic_records)

    step = 0
    while True:
        # Simulated inputs
        nodes["pir1"].state = step % 5 == 0
        nodes["switch1"].state = step % 9 == 0

        engine.tick(step)
        step += 1
        time.sleep(1)


if __name__ == "__main__":
    main()
