#include <stdexcept>
#include <string>

#include <ros/ros.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/TransformStamped.h>
#include <goal_sender_msgs/ApplyGoals.h>
#include <goal_sender_msgs/GoalSequence.h>
#include <tf2_ros/transform_listener.h>

inline double squaring_distance(const geometry_msgs::Point& a, const geometry_msgs::Point& b) {
  const auto x {a.x - b.x};
  const auto y {a.y - b.y};
  return x*x + y*y;
}

/**
 * Waypoint update service manager
 */
class WaypointManager
{
public:
  bool operator()(goal_sender_msgs::ApplyGoals::Request& req,
                  goal_sender_msgs::ApplyGoals::Response& res){
    return true;
  }

  const geometry_msgs::Point& point() const
  {
    if (is_end()) throw std::logic_error {"range error: Please check is_end() before point()"};
    return now_goal->position;
  }

  double radius() const
  {
    if (is_end()) throw std::logic_error {"range error: Please check is_end() before radius()"};
    return now_goal->radius;
  }

  bool next()
  {
    if (is_end()) throw std::logic_error {"range error: Please check is_end() before next()"}; // wrong way.
    if (++now_goal == sequence_.waypoints.end())
      return false; // correct way: this is last one.
    return true;
  }

  bool is_end() const
  {
    return sequence_.waypoints.end() == now_goal;
  }

  [[deprecated]]
  const goal_sender_msgs::Waypoint& get() const
  {
    if (is_end()) throw std::logic_error {"range error: Please check is_end() before get()"};
    return *now_goal;
  }

private:
  goal_sender_msgs::GoalSequence sequence_;
  decltype(sequence_.waypoints)::iterator now_goal;
};

/**
 * Tf lookup API
 */
class TfPositionManager
{
public:
  TfPositionManager(tf2_ros::Buffer& tfBuffer)
    : buffer_ {tfBuffer}
  {
  }

  geometry_msgs::Point operator()(std::string parent, std::string child) const
  {
    const auto ts {buffer_.lookupTransform(parent, child, ros::Time(0))};
    geometry_msgs::Point point;
    point.x = ts.transform.translation.x;
    point.y = ts.transform.translation.y;
    return point;
  }

private:
  tf2_ros::Buffer& buffer_;
};

class GoalSender
{
public:
  GoalSender(WaypointManager point_manager,
             TfPositionManager lookupper)
    : point_manager_ {point_manager},
      lookupper_ {lookupper}
  {
  }

  void once()
  {
    if (point_manager_.is_end())
      return; // no work
    if (is_reach()) {
      point_manager_.next();
    }
  }

private:
  bool is_reach() const
  {
    const auto robot_point {lookupper_("/map", "/base_link")};
    const auto waypoint_point {point_manager_.point()};
    const auto sqr_distance {squaring_distance(robot_point, waypoint_point)};

    const auto radius {point_manager_.radius()};
    const auto sqr_radius {radius * radius};

    if (sqr_distance < sqr_radius) // into valid range
      return true;
    return false;
  }

  WaypointManager point_manager_;
  TfPositionManager lookupper_;
};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "goal_sender");
  ros::NodeHandle nh {};

  WaypointManager point_manager {};
  ros::ServiceServer srv {
      nh.advertiseService<goal_sender_msgs::ApplyGoals::Request,
                          goal_sender_msgs::ApplyGoals::Response>(
          "apply_goals", point_manager)};

  tf2_ros::Buffer tfBuffer;
  tf2_ros::TransformListener tfListener {tfBuffer};
  TfPositionManager lookupper {tfBuffer};

  GoalSender goal_sender {point_manager, lookupper};

  ros::Rate rate {10};
  while (ros::ok()) {
    ros::spinOnce();
    goal_sender.once();
  }
}
