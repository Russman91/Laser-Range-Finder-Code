#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud.h>
#include <std_msgs/Int32.h>
#include <std_msgs/String.h>
#include <geometry_msgs/Point32.h>
#include <geometry_msgs/Polygon.h>
#include <boost/shared_ptr.hpp>
#include <art_lrf/Lines.h>
#include <Eigen/Eigen>
#include <cmath>
#include <vector>
#include <iostream>

#define MIN_VAL(X,Y) ((X < Y) ? (X) : (Y))
#define MAX_VAL(X,Y) ((X > Y) ? (X) : (Y))

using namespace std;
using namespace Eigen;

class line {

	public:
	    int theta_index;
	    int rho_index;
	    int votes;	
	    float est_rho;
	    vector<int> pixels;
	    vector<int> line_pixels;
	    vector<float> lengths;
	    vector<bool> partial_observe;
	    vector<vector<int> > support_points;
	    vector<vector<int> > endpoints;
	    vector<vector<float> > endpoint_ranges;
	    
	    
	 line()
    	{
		theta_index = 0;
		rho_index = 0;
        	pixels = vector<int>();
        	endpoints = vector<vector<int> >();
        	line_pixels = vector<int>();
    	}
};

bool doNextRun = false;
bool firstRun = true;
int est_rot = 0;
int previous_est_rot = 0;
Vector2f est_translation;
Vector2f previous_translation;

MatrixXd observed_projection(2,2);


vector<float> linspace(double min_range, double max_range, int total_no_points)  {
    vector<float> phi = std::vector<float>(total_no_points);
    phi[0] = min_range;
    double delta = (max_range-min_range)/(total_no_points - 1);
    for(int i = 1 ; i < phi.size() ; i++)
        phi[i]=phi[i-1]+delta;

    return phi;
}




class Compare {
	public:
		ros::NodeHandle nh;
		ros::Subscriber sub_lines;
		ros::Publisher pub_pos;
		std_msgs::String pos_msg_old;

		double counter;
		boost::shared_ptr<vector<line> > scan1, scan2, original_scan;

		Compare(ros::NodeHandle& _nh): nh(_nh) {
			pub_pos = nh.advertise<std_msgs::String>("/arduino/pos", 1);
			sub_lines = nh.subscribe("/lrfLines", 1, &Compare::lines_callback, this);	
			counter = 0;
		}

		~Compare() { 
			scan1.reset();
			scan2.reset();
		}


		int find_pos_index(float pos) {
			vector<float> temp;
	
			for (int i=0; i < 2001; i++) {
				temp.push_back(-10.0+(0.01*i));
			}
	
			int current_index = 0;
	
			for (int i=0; i<temp.size(); i++) {
				if (pos > temp[i]) {
					current_index = i;
				} else {
					break;
				}
			}
	
			return current_index;
		}


		int find_heading_index(int angle) {
			vector<int> temp;
	
			for (int i=0; i < 361; i++) {
				temp.push_back(-180+i);
			}
	
			int current_index = 0;
	
			for (int i=0; i<temp.size(); i++) {
				if (angle > temp[i]) {
					current_index = i;
				} else {
					break;
				}
			}
	
			return current_index;
		}


		void lines_callback(const art_lrf::Lines::ConstPtr& msg_lines) {

		if ((msg_lines->theta_index.size() != 0) && (msg_lines->est_rho.size() != 0) && (msg_lines->endpoints.size() != 0)) {
			
				
			scan2.reset(new vector<line> ());
			line temp;
			geometry_msgs::Point32 temp2;
			geometry_msgs::Point32 temp_theta_index;
			vector<int> temp3;

			for (int i = 0; i < msg_lines->theta_index.size(); i++) {
				temp.theta_index = msg_lines->theta_index[i];
				temp.est_rho = msg_lines->est_rho[i];
				
				for (int j = 0; j < msg_lines->endpoints[i].points.size(); j++) {
					temp2 = msg_lines->endpoints[i].points[j];
					temp3.push_back(temp2.x);
					temp3.push_back(temp2.y);
					temp.endpoints.push_back(temp3);
				}
				
				
				scan2->push_back(temp);
				temp.endpoints.clear();
			}

			if (firstRun == true){
				firstRun = false;
				original_scan = scan2;
				previous_translation << 0,0;
			}
			if (doNextRun == false)
			{
				
				//scan1 = scan2;
				scan1 = original_scan;
				doNextRun = true;
			}
			else
			{

			//cout << endl << "Scan 2 start:  " << scan2->at(0).theta_index << endl; 
				scan1 = original_scan;
    			
    			float delta_rho = 0.2;
    			float angle_increment =  0.00613592;
    			int counter;
				float delta_theta = 1;
				float pi = 3.1415926;
				int no_theta_entries = (2 * M_PI - delta_theta*angle_increment)/(delta_theta*pi/180) + 1;
				vector<float> theta = vector<float>(no_theta_entries);

				for(double i = -pi, counter = 0; counter < no_theta_entries; i+= delta_theta*pi/180, counter++) {
					theta[counter] = i;
					
				}
				
    			
    			
    			
    
				previous_est_rot = est_rot;
    
    			//Initial rotation estimation. This will vary depending on the lines being compared.
    			float rotation_prior_mean = previous_est_rot;
    			float rotation_prior_sd = 15;
    			float rotation_posterior_sd = 4;
 
    			vector<float> rotation_prior (360,0);
    			vector<float> rotation_score (360,0);
    
		    	float dist;
    			float sum1;
    			for (int i=0; i<360; i++)
    			{
        			dist = min(abs(i - rotation_prior_mean),abs(i-360-rotation_prior_mean));
        			rotation_prior[i] = exp(-pow(dist,2)/pow(rotation_prior_sd,2));
        			sum1 += rotation_prior[i];
    			}
    			for (int i=0; i<360; i++)
    			{
        			rotation_prior[i] /= sum1;
    			}
    
    
    			/*
     			for each pair of lines between the two scans, estimate the probability
     			%that they are from the same object. The more likely they are to be
     			%generated by an unmoved object, the more likely that the rotation of the
     			%helicopter was equal to the theta difference between the lines.
     			*/
    
		    	
    
				if (doNextRun == true) {
					for (int i=0; i < scan2->size(); i++)
					{
		    			for (int j=0; j < scan1->size(); j++)
		    			{
		        				float dist;
		        				float theta_diff = (delta_theta*(scan2->at(i).theta_index - scan1->at(j).theta_index) );
		        				if (theta_diff < 0) {
		        					theta_diff += 360;
		        				}
		        				for (int k=0; k<360; k++)
		        				{
		            				dist = min(abs(k-theta_diff), abs(k-theta_diff-360));
		            				rotation_score[k] = rotation_score[k] + exp(-pow(dist,2)/pow(rotation_posterior_sd,2));
		        				}
		    			}
					}
				}
				vector<float> rotation_prob(360,0);

				for (int i=0; i<rotation_prob.size();i++){
		    			rotation_prob[i] = rotation_prior[i]* rotation_score[i];
						//rotation_prob[i] =  rotation_score[i];
				}
		
				float max1 = 0;
				
				
				for (int i=0; i<rotation_prob.size();i++){
		    		if (rotation_prob[i]> max1){
		       			max1 = rotation_prob[i];
		        		est_rot = i;
		    		}
				}
				
				cout << "est rot: " << est_rot << endl;

    			vector<vector<int> > matched_lines;
				if (doNextRun = true) {    
				
					for (int i=0; i < scan2->size(); i++)
					{
						for (int j=0; j < scan1->size();j++)
						{
								if (min(abs(delta_theta*(scan2->at(i).theta_index - scan1->at(j).theta_index) - est_rot), abs(delta_theta*(scan2->at(i).theta_index - scan1->at(j).theta_index) - est_rot + 360))< 10)
								{
									vector<int> temp_vector(2,0);
									temp_vector[0] = i; temp_vector[1] = j;
									matched_lines.push_back(temp_vector);
									break;
								}
						}
					}
		
					 		

					MatrixXf rho_change(matched_lines.size(),1);
					for (int i=0; i<matched_lines.size(); i++)
					{   
						rho_change(i,0) = scan2->at(matched_lines[i][0]).est_rho - scan1->at(matched_lines[i][1]).est_rho;
					}

					MatrixXf A(matched_lines.size(),2);

					for (int i=0; i<matched_lines.size(); i++)
					{
						A(i,0) = cos(theta[scan1->at(matched_lines[i][1]).theta_index]);
						A(i,1) = sin(theta[scan1->at(matched_lines[i][1]).theta_index]);
						
						cout << A(i,0) << "  " << A(i,1) << endl;
					}

			
					est_translation = A.jacobiSvd(ComputeThinU | ComputeThinV).solve(rho_change);
		   			//est_translation << 0,0;
	
					float condition_threshold = 0.2;

					Matrix2f C = A.transpose()*A;
					SelfAdjointEigenSolver<Matrix2f> eigensolver(C);
					Vector2f Eigenvalues = eigensolver.eigenvalues();
					Matrix2f Eigenvectors = eigensolver.eigenvectors();

					if (Eigenvalues(0) == 0) {
						Eigenvalues(0) = 0.000001;
					}
					if (Eigenvalues(1) == 0) {
						Eigenvalues(1) = 0.000001;
					}
					cout << "condition number: " << Eigenvalues(0)/Eigenvalues(1) << endl;
					if (Eigenvalues(0)/Eigenvalues(1) < condition_threshold || Eigenvalues(0)/Eigenvalues(1) > 1/condition_threshold)
					{
						int principal_eigenvalue = 0;
						if (Eigenvalues(1) > Eigenvalues(0))
							principal_eigenvalue = 1;
				
						MatrixXd unobserved_direction(2,1);				
						unobserved_direction(0) = Eigenvectors(0,1-principal_eigenvalue);
						unobserved_direction(1) = Eigenvectors(1,1-principal_eigenvalue);

						MatrixXd observed_direction(2,1);// = Eigenvectors(0,1,2,1);
						observed_direction(0) = Eigenvectors(0,principal_eigenvalue);
						observed_direction(1) = Eigenvectors(1,principal_eigenvalue);
						
						cout << "principal_eigenvalue: " << principal_eigenvalue << endl;
						cout << "Eigenvectors: " << endl;
						cout << Eigenvectors;
				
						observed_projection = observed_direction*(observed_direction.transpose()*observed_direction).inverse()*observed_direction.transpose();
				
					}
					else {
						observed_projection << 1, 0,
									0, 1;
					}

					Vector2f current_translation = A.jacobiSvd(ComputeThinU | ComputeThinV).solve(rho_change);
					//Vector2f current_translation;
					//current_translation << 0,0;
					Matrix2f observed_projection2f;
					observed_projection2f(0,0) = observed_projection(0,0);
					observed_projection2f(0,1) = observed_projection(0,1);
					observed_projection2f(1,0) = observed_projection(1,0);
					observed_projection2f(1,1) = observed_projection(1,1);

					est_translation = observed_projection2f * current_translation + (previous_translation - observed_projection2f*previous_translation);

					/*Matrix2f R;
					R << cos(est_rot*pi/180), -sin (est_rot*pi/180),
						 sin(est_rot*pi/180), cos(est_rot*pi/180);			
	
					Vector2f rotated_translation = R*est_translation;

					float max_translation = 0.1;
						
					if (rotated_translation(0) - previous_translation(0) > max_translation)
						rotated_translation(0) = previous_translation(1) + max_translation;
					if (rotated_translation(0) - previous_translation(0) < -max_translation)
						 rotated_translation(0) = previous_translation(1) - max_translation;
					if (rotated_translation(1) - previous_translation(1) > max_translation)
						rotated_translation(1) = previous_translation(1) + max_translation;
					if (rotated_translation(1) - previous_translation(1) < -max_translation)
						 rotated_translation(1) = previous_translation(1) - max_translation;
					*/
	
					//previous_translation = rotated_translation;
					previous_translation = est_translation;

					//cout << endl << "SUCCESS!!" << endl;
					if (est_rot > 180) {				
						 est_rot -= 360;
						 
					}
					cout << endl << "Estimated Rotation:  " << est_rot;
					cout<< "   Estimated Translation:  " << est_translation(0) << "   " << est_translation(1) << "   MatchedLines: " << matched_lines.size() << endl; 
					//scan1 = scan2;
					scan1 = original_scan;

					union PosPacket {
						struct {
							int x;
							int y;
							int theta;
						};
						char data[12];
					} pos;

					pos.x = find_pos_index(est_translation(0));
					pos.y = find_pos_index(est_translation(1));
					pos.theta = find_heading_index(est_rot);

					std_msgs::String pos_msg;
	
					pos_msg.data = pos.data;
					pub_pos.publish(pos_msg);
	
					pos_msg_old.data = pos_msg.data;
				}		
			}
		}
	
		else {
			cout << endl << "MISSED SCAN" << endl;
			pub_pos.publish(pos_msg_old);
		}

 	}
};



int main (int argc, char** argv) {
	ros::init(argc, argv, "odometry");
	ros::NodeHandle nh;

	Compare compare(nh);

	ros::Rate loop_rate(2);
	while(ros::ok()) {
		ros::spinOnce();
		loop_rate.sleep();
	}
	return 0;

/*


	vector<line> scan2;
	vector<float> angle1;

float window_size = 1.0;
float window_tolerance = 0.2;
int window_line = -1;
int window_gap = 0;

MatrixXd p1(2,1);                       //position of first window edge
MatrixXd p2(2,1);                       //position of second window edge
MatrixXd p(2,2);                        //Matrix of window edges
MatrixXd current_position(2,1);
MatrixXd window_center(2,1);
MatrixXd slope(2,1);
MatrixXd perpindicular(2,1);
MatrixXd norm_current_position(2,1);
MatrixXd projection(2,2);
MatrixXd middle_objective(2,1);
MatrixXd objective_error(2,1);
MatrixXd target(2,1);
MatrixXd motion(2,1);
MatrixXd new_position(2,1);
float middle_error;


for (int i=0; i<scan2.size(); i++)
{
	for (int j=1; j<scan2[i].lengths.size(); j=j+2)
	{
		if (abs(scan2[i].lengths[j]-window_size) < window_tolerance){
			window_line = i;
			window_gap = (j-1)/2;
		}
	}	
}

vector<int> gap_points(2,0);

if (window_line >= 0){
	gap_points[0] = scan2[window_line].endpoints[window_gap][1];
	gap_points[1] = scan2[window_line].endpoints[window_gap+1][0];
	p1(0) = scan2[window_line].endpoint_ranges[window_gap][1]*cos(angle1[scan2[window_line].theta_index]);
	p1(1) = scan2[window_line].endpoint_ranges[window_gap][1]*sin(angle1[scan2[window_line].theta_index]);
	p2(0) = scan2[window_line].endpoint_ranges[window_gap+1][0]*cos(angle1[scan2[window_line].theta_index]);
	p2(1) = scan2[window_line].endpoint_ranges[window_gap+1][0]*sin(angle1[scan2[window_line].theta_index]);

	p1 << 5,3;
	p2 << 5-pow(2,.5), 3-pow(2,.5) + 2;


	p << p1(0),p2(0),
	     p1(1), p2(1);

	float forward_step = 0.1;


	current_position << 5,1;
	//%current_position = new_position;

	window_center = (p1 + p2)/2;


	slope = p1 - p2;
	slope = slope/pow(pow(slope(0),2) + pow(slope(1),2),0.5);
	float yaw = -asin(slope(1));
	float yaw_d = yaw*180/3.1415;

	perpindicular << -slope(1); slope(0);
	norm_current_position = current_position - window_center;
	projection = perpindicular*(perpindicular.transpose()*perpindicular).inverse()*perpindicular.transpose();
	middle_objective = projection * norm_current_position;
	objective_error = norm_current_position - middle_objective;
	middle_error = pow(pow(objective_error(0),2) + pow(objective_error(1),2),0.5);

	if (middle_error  < 0.25)
	{
	    MatrixXd temp0(1,1);
	    temp0 = (norm_current_position.transpose()*perpindicular);
	    float temp1 = temp0(0,0)>0? 1:-1;
	    target = projection*(norm_current_position - temp1*forward_step*perpindicular);
	} else
	{
	    target = projection*norm_current_position;
	}

	motion = target - norm_current_position;
	new_position = current_position + motion;
}
*/
}


