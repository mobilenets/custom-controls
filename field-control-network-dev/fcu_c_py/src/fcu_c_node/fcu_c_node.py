import rclpy
from rclpy.node import Node
from std_msgs.msg import String

class FCUSupervisor(Node):
    def __init__(self):
        super().__init__('fcu_c_node')
        # Subscribe to FCU-µ heartbeat (if it exists)
        self.subscription = self.create_subscription(
            String,
            'fcu_mu/heartbeat',
            self.listener_callback,
            10
        )
        # Optional: publisher to control FCU-µ
        self.publisher = self.create_publisher(String, 'fcu_mu/control', 10)

    def listener_callback(self, msg):
        self.get_logger().info(f'Received from FCU-µ: "{msg.data}"')

def main(args=None):
    rclpy.init(args=args)
    node = FCUSupervisor()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()

