#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/PointCloud.h>
#include <std_msgs/Int32.h>
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



bool firstRun = true;

class line {

public:
    int theta_index;
    int rho_index;
    float est_rho;
    float est_theta;
    int votes;
    vector<int> pixels;
    vector<int> vote_pixels;
    vector<int> abstain_pixels;
    vector<int> line_pixels;
    vector<float> lengths;
    vector<bool> partial_observe;
    vector<vector<int> > support_points;
    vector<vector<int> > endpoints;
    vector<vector<float> > endpoint_ranges;
    
    line()
    {
        pixels = vector<int>();
        endpoints = vector<vector<int> >();
        line_pixels = vector<int>();
		lengths = vector<float>();
		endpoint_ranges = vector<vector<float> >();
		partial_observe = vector<bool>();
		support_points = vector<vector<int> >();
    }
};



class line_finder {

	public:
	ros::NodeHandle nh;
	ros::Subscriber sub_scan;
	ros::Publisher pub_cloud;
	ros::Publisher pub_lines;

	boost::shared_ptr<vector<line> > current_lines, old_lines;
	
	line_finder(ros::NodeHandle& _nh): nh(_nh) {
			pub_cloud = nh.advertise<sensor_msgs::PointCloud>("/world", 1);
			pub_lines = nh.advertise<art_lrf::Lines>("/lrfLines", 1);
			sub_scan = nh.subscribe("/scan", 1, &line_finder::lrf_callback, this);
		}

		~line_finder() { 
			old_lines.reset();
			current_lines.reset();
		}
	

vector<float> line_tuner(vector<line> lines, float line_num, vector<float> current_scan, vector<float> theta, vector<float> rho, vector<float> phi){
    
    vector<float> x_vector(lines[line_num].line_pixels.size(),0);
    vector<float> y_vector(lines[line_num].line_pixels.size(),0);

    for (int i=0; i<lines[line_num].line_pixels.size(); i++){
        x_vector[i] = current_scan[lines[line_num].line_pixels[i]]*cos(phi[lines[line_num].line_pixels[i]]);
        y_vector[i] = current_scan[lines[line_num].line_pixels[i]]*sin(phi[lines[line_num].line_pixels[i]]);
    }

    float theta_b = theta[lines[line_num].theta_index];
    int rho_sign = theta_b>0?1 :-1;
    double est_rho = rho[lines[line_num].rho_index];
                           
    float sigma = 0.05;

    int tot_iterations = 200;
    double grad_rho = 0;
    double grad_theta_b = 0;

    for (int iteration=0; iteration<tot_iterations; iteration++)
    {
        float new_x;
        float new_y;
        float dist;
        int dist_sign;
        
        grad_rho = 0;
        grad_theta_b = 0;
        
        for (int i=0; i<x_vector.size(); i++){
            new_x = x_vector[i] - est_rho*cos(theta_b);
            new_y = y_vector[i] - est_rho*sin(theta_b);
            dist = cos(theta_b)*new_x + sin(theta_b)*new_y;
            dist_sign = dist>0?1:-1;
            dist = abs(dist);
            grad_rho += (pow(dist,2) + 2*dist*sigma)/(pow(dist,2) + 2*dist*sigma + pow(sigma,2))*dist_sign;
            grad_theta_b += (pow(dist,2) + 2*dist*sigma)/(pow(dist,2) + 2*dist*sigma + pow(sigma,2))*dist_sign*(new_x*cos(theta_b - 3.1415/2) + new_y*sin(theta_b - 3.1415/2));
        }
        
        est_rho += grad_rho/20000;
        theta_b += grad_theta_b/20000;        
    }
    cout << est_rho << endl;
	vector<float> result;
	result.push_back(est_rho);
	result.push_back(theta_b);
                                          
    return result;
}

vector<float> rotAdjust(int line_number, MatrixXd R, vector<line> lines) {
	vector<float> a(2,0);
	return a;
	

    MatrixXd p1(3,1);
    MatrixXd p1_adjusted(3,1);
    MatrixXd p2(3,1);
    MatrixXd p2_adjusted(3,1);
    MatrixXd p_diff(3,1);
    
    float new_theta;
    float new_rho;
    
    vector<float> output;
    
    p1(0) = lines[line_number].est_rho * cos(lines[line_number].est_theta);
    p1(1) = lines[line_number].est_rho * sin(lines[line_number].est_theta);
    p1(2) = 0;
    
    p2(0) = p1(0) + cos(lines[line_number].est_theta - 3.1415/2);
    p2(1) = p1(1) + sin(lines[line_number].est_theta - 3.1415/2);
    p2(2) = 0;
    
    p1_adjusted = R.transpose() * p1;
    p2_adjusted = R.transpose() * p2;
    
    p_diff = p2_adjusted - p1_adjusted;
    if (p_diff(0) != 0)
    {
        new_theta = atan(p_diff(1)/p_diff(0));
        if (p_diff(0) < 0)
        {
            if (p_diff(1) > 0)
            {
                new_theta -= 3.1415;
            } else
            {
                new_theta += 3.1415;
            }
        }
    }
    else
    {
        if (p_diff(1) >= 0)
        {
            new_theta = 3.1415/2;
        }
        else
        {
            new_theta = -3.1415/2;
        }
    }
    
    new_theta += 3.1415/2;
    if (new_theta > 3.1415)
    {
        new_theta -= 2*3.1415;
    }
    
    new_rho = pow(pow(p1(0)*cos(new_theta),2) + pow(p1(1)*sin(new_theta),2),0.5);
    lines[line_number].est_theta = new_theta;
    lines[line_number].est_rho = new_rho;
    
    output[0] = new_theta;
    output[1] = new_rho;
    
    return output;
        
}


vector<float> linspace(double min_range, double max_range, int total_no_points)  {
    vector<float> phi = std::vector<float>(total_no_points);
    phi[0] = min_range;
    double delta = (max_range-min_range)/(total_no_points - 1);
    for(unsigned int i = 1 ; i < phi.size() ; i++)
        phi[i]=phi[i-1]+delta;

    return phi;
}



void lrf_callback(const sensor_msgs::LaserScan::ConstPtr& msg_lrf) {
	ros::Subscriber sub_lines;
	vector<float> current_scan;
	current_lines.reset(new vector<line> ());

	for (unsigned int i = 0; i < msg_lrf->ranges.size(); i++) {
    	current_scan.push_back(msg_lrf->ranges[i]);
    }
        
        
		
	
	
	/*
	int tot_scans = 681;
   	float min_range = 0.02;
    float max_range = 5.6;
    double min_angle = -2.08621;
    double max_angle = 2.08621;
    double angle_increment = 0.00613592;
    */
    
    int tot_scans = msg_lrf->ranges.size();
    float min_range = msg_lrf->range_min;
    float max_range = msg_lrf->range_max;
    double min_angle = msg_lrf->angle_min;
    double max_angle = msg_lrf->angle_max;
    double angle_increment = msg_lrf->angle_increment;
    
    //cout << min_range << "   " << max_range << "  " << angle_increment;
    	
	
	int line_counter = 0;
	vector<float> detected_thetas = vector<float>();
	vector<float> detected_rhos = vector<float>();
	int rho_destruct_radius = 3;
	int theta_destruct_radius = 10;

	vector <line> lines = std::vector<line>();

	float max_range_threshold = max_range-0.5;
	int max_gap = 10;
	
	int detection_threshold = 20;
	int hough_detection_threshold = 2;
	float hough_theta_sd = 3;
	float hough_rho_sd = 1;
	
	int out_of_view = 3;


	vector<float> phi = linspace(min_angle, max_angle, tot_scans);


	int counter;
	float delta_theta = 1;
	int no_theta_entries = (2 * M_PI - delta_theta*angle_increment)/(delta_theta*M_PI/180) + 1;
	vector<float> theta = vector<float>(no_theta_entries);

	for(double i = -M_PI, counter = 0; counter < no_theta_entries; i+= delta_theta*M_PI/180, counter++) {
	    theta[counter] = i;
	}    

	float delta_rho = .2;
	float rho_min = .2;
	float rho_max = 6;

	int rho_length = (rho_max - rho_min)/delta_rho + 1;
	vector<float> rho = vector<float>(rho_length);

	for (double i = rho_min, counter = 0; counter < rho_length; i += delta_rho, counter++) {
	    rho[counter] = i;
	}
	
	vector<float> rho_votes(tot_scans,0);
	vector<float> theta_votes(tot_scans,0);
	vector<int> voted(tot_scans, 0);
	int arc_width = 4;

	for (int i = 1+arc_width; i<tot_scans-arc_width; i++)
	{
    	vector<float> arc_angles;
    	for (int j=-arc_width;j<=arc_width; j++){
    	    arc_angles.push_back(j*angle_increment);
    	}
    	vector<float> local_r;
    	for (int j=i-arc_width;j<=i+arc_width; j++){
    	    local_r.push_back(current_scan[j]);
    	}
    	vector<int> nonzeros;
    	for (int j=0; j<local_r.size(); j++){
    	    if (local_r[j] > min_range){
    	        nonzeros.push_back(j);
    	    }
    	}
    	if (local_r.size()-nonzeros.size() > 3){
    	    continue;
    	}
    	MatrixXd arc_anglesXd(nonzeros.size(),1);
    	MatrixXd local_rXd(nonzeros.size(),1);
    	MatrixXd axial(nonzeros.size(),1);
    	MatrixXd rotational(nonzeros.size(),1);
    	MatrixXd A(nonzeros.size(),2);
    
    	for (int j=0; j<nonzeros.size(); j++){
    	    arc_anglesXd(j) = arc_angles[nonzeros[j]];
    	    local_rXd(j) = local_r[nonzeros[j]];
    	    axial(j) = cos(arc_anglesXd(j))*local_rXd(j);
    	    rotational(j) = sin(arc_anglesXd(j))*local_rXd(j);
    	    A(j,0) = 1;
    	    A(j,1) = rotational(j);
    	}
    	
    	MatrixXd X(2,1);
    	X = A.jacobiSvd(ComputeThinU | ComputeThinV).solve(axial);
    	
    	float slope = X(1);
    	float error = 0;
    	for (int j=0; j<nonzeros.size(); j++){
    	    error += pow(axial(j) - (slope*rotational(j) + X(0)),2)/nonzeros.size();
    	}
    	float angle_diff = atan(slope);
    	
    	if (error < 0.01)
    	{
    	    for (int j=0; j<nonzeros.size(); j++)
    	    {
    	        rho_votes[i] += cos(arc_anglesXd(j))*local_rXd(j)*cos(angle_diff)/nonzeros.size();
    	        theta_votes[i] += (phi[i] - angle_diff)/nonzeros.size();
    	    }
    	    voted[i] = 1;
    	}
	}
	
	MatrixXd H(theta.size(), rho.size());
	H = MatrixXd::Zero(theta.size(), rho.size());
	
	int H_counter1 = 0;
	
	for (int i=0;i<tot_scans;i++)
	{
    	if (voted[i] == 1)
    	{
    		H_counter1++;
    	    int temp_thetaInd = theta.size()-1;
    	    int temp_rhoInd = rho.size()-1;
    	    for (int j=0;j<theta.size();j++)
    	    {
    	        if (theta[j]>theta_votes[i])
    	        {
    	            temp_thetaInd = j;
    	            break;
    	        }            
    	    }
    	    for (int j=0;j<rho.size();j++)
    	    {
    	        if (rho[j]>rho_votes[i])
    	        {
    	            temp_rhoInd = j;
    	            break;
    	        }            
    	    }

    	    H(temp_thetaInd, temp_rhoInd) += 2;
    	}
	}
	
	MatrixXd H_smoothed1(theta.size(), rho.size());
	MatrixXd H_smoothed2(theta.size(), rho.size());
	H_smoothed1 = MatrixXd::Zero(theta.size(), rho.size());
	H_smoothed2 = MatrixXd::Zero(theta.size(), rho.size());
	
	for (int i=0; i<theta.size(); i++)
	{
		for (int j=0; j<rho.size(); j++)
		{
			for (int delta_i=-(int)(3*hough_theta_sd); delta_i<= (int)(3*hough_theta_sd); delta_i++)
			{
				if ((i+delta_i >= 0) && (i + delta_i <= theta.size()-1))
				{
					H_smoothed1(i, j) += H(i+delta_i,j) * 1/(pow(2*3.1415,0.5)*hough_theta_sd)*exp(-pow(delta_i,2)/(2*pow(hough_theta_sd,2)));
				}
			}
		}
	}
	for (int i=0; i<theta.size(); i++)
	{
		for (int j=0; j<rho.size(); j++)
		{
			for (int delta_j=-(int)(3*hough_rho_sd); delta_j<= (int)(3*hough_rho_sd); delta_j++)
			{
				if ((j+delta_j >= 0) && (j + delta_j <= rho.size()-1))
				{
					H_smoothed2(i, j) += H_smoothed1(i,j+delta_j) * 1/(pow(2*3.1415,0.5)*hough_rho_sd)*exp(-pow(delta_j,2)/(2*pow(hough_rho_sd,2)));
				}
			}
		}
	}
	
	for (int i=0; i<theta.size(); i++)
	{
		for (int j=0; j<rho.size(); j++)
		{
			H(i,j) = (int)H_smoothed2(i,j);
		}
	}
	
	
	if (firstRun != true)
	{
		for (int i = 0; i<old_lines->size(); i++)
		{
			for (int j=-10; j<(10+1); j++)
			{
				if ((old_lines->at(i).theta_index + j >= 0) && (old_lines->at(i).theta_index + j <= theta.size() - 1))
				{
					for (int k=-3; k<(3+1); k++)
					{
						if ((old_lines->at(i).rho_index + k >= 0) && (old_lines->at(i).rho_index + k <= rho.size()-1))
						{
							H(old_lines->at(i).theta_index+j, old_lines->at(i).rho_index+k) *= 2.0;
						}
					}
				}
			}
		}
	}
	
	
	for (int i=0; i<theta.size(); i++)
	{
		for (int j=0; j<rho.size(); j++)
		{
			cout << H(i,j) << " ";
			if (H(i,j) < 10)
				cout << " ";
		}
		cout << endl;
	}
	
	cout << endl << endl << "---------------------------------------" <<endl << endl;
	
	
	
	cout << "number of voters: " << H_counter1 << endl;
	
	
	
	int counter3 = 0;
	sensor_msgs::PointCloud cloud;
	cloud.header.frame_id = "/world";
	cloud.points.resize((theta.size()*rho.size()));
	for (unsigned int i = 0; i < rho_votes.size(); i++){


		cloud.points[counter3].x = i;
		cloud.points[counter3].y = 0;
		cloud.points[counter3].z = rho_votes[i];
		counter3++;
		
	}

    pub_cloud.publish(cloud);

   
    
    int rho_maxima_range_length = ((rho.size() - 1)/(2*rho_destruct_radius)) + 1;
    float rho_max_range_delta = 2*rho_destruct_radius;
    vector<int> rho_maxima_range = vector<int>(rho_maxima_range_length);
    counter = 0;
    
    for (double i = 0; counter < rho_maxima_range_length; i += rho_max_range_delta, counter++)
		rho_maxima_range[counter] = i;
    
    if ((unsigned int)rho_maxima_range[counter - 1] < rho.size()) 
        rho_maxima_range.push_back(rho.size()); 
    
    int theta_maxima_range_length = ((theta.size() - 1)/(2*theta_destruct_radius)) + 1;
    float theta_max_range_delta = 2*theta_destruct_radius;
    vector<int> theta_maxima_range = vector<int>(theta_maxima_range_length);
    
    for (double i = 0, counter = 0; counter < theta_maxima_range_length; i += theta_max_range_delta, counter++) {
        theta_maxima_range[counter] = i;
	}
    
    
    if ((unsigned int)theta_maxima_range[theta_maxima_range.size() - 1] < theta.size()) {
        theta_maxima_range.push_back(theta.size()); 
    }
    
    
    MatrixXd H_maxima(theta_maxima_range.size(), rho_maxima_range.size());
    H_maxima = MatrixXd::Zero(theta_maxima_range.size(), rho_maxima_range.size());    

    for(unsigned int i = 0; i < (theta_maxima_range.size() - 1); i++)
    {
        for(unsigned int j = 0; (j < rho_maxima_range.size() - 1); j++)
        {
     
            
            float max_val = H(theta_maxima_range[i], rho_maxima_range[j]);
            
            for(int p = theta_maxima_range[i]; p <= theta_maxima_range[i+1] - 1; p++){
                for(int q = rho_maxima_range[j]; q <= rho_maxima_range[j+1] - 1; q++)
                {   
					if (max_val < H(p,q)) 
                        max_val = H(p,q);
                }
            }
            
            H_maxima(i, j) = max_val;
        }
    }
    
            
    MatrixXd H_search(theta_maxima_range.size(), rho_maxima_range.size());
    
    for(unsigned int i = 0; i < theta_maxima_range.size(); i++)
    {
        for(unsigned int j = 0; j < rho_maxima_range.size(); j++)
        {
			H_search(i,j) = (H_maxima(i,j) >= hough_detection_threshold) ? 1 : 0; 
		}
	}
    
    int too_close;
    for(unsigned int H1_index = 0; H1_index < theta_maxima_range.size(); H1_index++)
    {
        for(unsigned int H2_index = 0; H2_index < rho_maxima_range.size(); H2_index++)
        {
            if(H_search(H1_index, H2_index) == 0)
                continue;
            
            for(int theta_index = theta_maxima_range[H1_index]; theta_index < theta_maxima_range[H1_index + 1]; theta_index++)
            {
                for(int rho_index = rho_maxima_range[H2_index]; rho_index < rho_maxima_range[H2_index + 1]; rho_index++)
                {
                    if(H(theta_index, rho_index) == H_maxima(H1_index, H2_index))
                    {
                        too_close = 0;
                    }
                    else
                    {
                    	too_close = 1;
                    	continue;
                    }
                    
                    for(int i = MAX_VAL(0, theta_index - theta_destruct_radius); i < MIN_VAL((int)theta.size(), theta_index + theta_destruct_radius); i++)
                    {
                        for(int j = MAX_VAL(0, rho_index - rho_destruct_radius); j < MIN_VAL((int)rho.size(), rho_index + rho_destruct_radius); j++)
                        {
							if (H(i,j) > H(theta_index, rho_index)) {
                                too_close = 1;
                            }
                            
                            if(H(i,j) == H(theta_index, rho_index))
                            {
                                if(i < theta_index)
                                    too_close = 1;
                                else if (j < rho_index) {
                                    too_close = 1;
                                }
                            }                                
                        }
                    }
                    
                    if (too_close == 0) {
                        detected_thetas.push_back(theta_index);
                        detected_rhos.push_back(rho_index);
                        
						line obj;
						obj.theta_index = theta_index;
						obj.rho_index = rho_index;
						obj.votes = H(theta_index, rho_index);
						obj.pixels.clear();
						lines.push_back(obj);
						cout << obj.theta_index << endl;
                        
                        
			line_counter++;
                    }
                }
            }    
        }
    }
    
    


    for(unsigned int i = 0; i < current_scan.size(); i++)
    {
    	if (voted[i] == 1)
		{
    		for (int j=0;j<lines.size();j++)
    		{
    		    if ( (abs(theta[lines[j].theta_index] - theta_votes[i])*180/3.1415 < 10) && 
    		        (abs(rho[lines[j].rho_index] - rho_votes[i])<=0.5) )
    		    {
    		        lines[j].vote_pixels.push_back(i);
    		    }
    		}
		} else
		{
        	for(unsigned int theta_index = 0; theta_index < detected_thetas.size(); theta_index++)
        	{
            	float r = cos(abs(phi[i] - theta[detected_thetas[theta_index]]))*current_scan[i];
			
            	if(r < rho_min)
            	    continue;
            	else 
            	    if(r > rho_max)
            	        continue;
            	    else
            	        r = r - rho_min;
            	
            	int r_index = floor(r/delta_rho);
            	for(unsigned int line_num = 0; line_num < lines.size(); line_num++)
            	{
            	    if ((abs(lines[line_num].theta_index - detected_thetas[theta_index]) < 2) && (abs(lines[line_num].rho_index - r_index - 1) < 2)) {
            	        lines[line_num].abstain_pixels.push_back(i);
            	    }
            	}
        	}
    	}
    }

	for (int i=0;i<lines.size();i++)
	{
		if (lines[i].vote_pixels.size() == 0)
		{
			lines[i].pixels = lines[i].abstain_pixels;
			continue;
		}
		if (lines[i].abstain_pixels.size() == 0)
		{
			lines[i].pixels = lines[i].vote_pixels;
			continue;
		}
			
		int vote_index = 0;
		int abstain_index = 0;
		while ( (vote_index < lines[i].vote_pixels.size()) && (abstain_index < lines[i].abstain_pixels.size()) )
		{
			if (lines[i].vote_pixels[vote_index] < lines[i].abstain_pixels[abstain_index])
			{
				lines[i].pixels.push_back(lines[i].vote_pixels[vote_index]);
				vote_index++;
			} else
			{
				if (lines[i].abstain_pixels[abstain_index] < lines[i].vote_pixels[vote_index])
				{
				lines[i].pixels.push_back(lines[i].abstain_pixels[abstain_index]);
				abstain_index++;
				} else // both are equal
				{
					lines[i].pixels.push_back(lines[i].abstain_pixels[abstain_index]);
					abstain_index++;
					lines[i].pixels.push_back(lines[i].vote_pixels[vote_index]);
					vote_index++;
				}
			}
		}
		if (vote_index < lines[i].vote_pixels.size())
		{
			while (vote_index<lines[i].vote_pixels.size())
			{
				lines[i].pixels.push_back(lines[i].vote_pixels[vote_index]);
				vote_index++;
			}
		}
		if (abstain_index < lines[i].abstain_pixels.size())
		{
			while (abstain_index<lines[i].abstain_pixels.size())
			{
				lines[i].pixels.push_back(lines[i].abstain_pixels[abstain_index]);
				abstain_index++;
			}
		}
	}
		
		
    for(unsigned int line_num = 0; line_num < lines.size(); line_num++)
    {
        vector<int> pixels = lines[line_num].pixels;
        if (pixels.size() == 0)
        {
        	continue;
        }
        bool occluded = true;
		bool max_range_line = false;
        int pixel_counter = 0;
        int line_start = pixels[0];
        int line_start_index = 0;
        int max_range_counter = 0;
		float expected_dist = 0.0;
        vector<int> temp_endpoints(2,0);
		vector<float> temp_endpoint_ranges(2,0);
        vector<int> temp_support_points(2,0);

        for (unsigned int i=1; i<pixels.size(); i++)
        {
            if (pixel_counter == 1)
            {
                line_start = pixels[i];
                line_start_index = i;
                if (current_scan[pixels[i]] > max_range_threshold || current_scan[pixels[i]] < out_of_view)
                {
                    max_range_line = true;
                } 
                else
                {
                    max_range_line = false;
                }
            }

            if (pixels[i] - pixels[i-1] <= max_gap)
            {
                pixel_counter += 1;
            }
            else
            {
                occluded = true;
                max_range_counter = 0;
                for (int j=pixels[i-1]; j<pixels[i]; j++)
                {
                    if (current_scan[j] < min_range)
                    {
                        max_range_counter += 1;
                    }
                    expected_dist = abs(rho[lines[line_num].rho_index]/cos(phi[j]-theta[lines[line_num].theta_index]+3.1415/2));
                    if (current_scan[j] > expected_dist || max_range_counter >= 2)
                    {
                        occluded = false;
                    }
                }
                if (occluded == false)
                {
                    if (pixel_counter > detection_threshold)
                    {
                        temp_endpoints[0] = line_start;
                        temp_endpoints[1] = pixels[i-1];
                        lines[line_num].endpoints.push_back(temp_endpoints);
			temp_endpoint_ranges[0] = current_scan[line_start];
			temp_endpoint_ranges[1] = current_scan[pixels[i-1]];
			lines[line_num].endpoint_ranges.push_back(temp_endpoint_ranges);


                        for (unsigned int k=line_start_index; k<i-1; k++)
                        {
                            lines[line_num].line_pixels.push_back(lines[line_num].pixels[k]);
                        }
                        if (current_scan[pixels[i]] > max_range_threshold || pixels[i] > tot_scans - out_of_view)
                        {
                            max_range_line = true;
                        }
                        lines[line_num].partial_observe.push_back(max_range_line);
                        pixel_counter = 1;                
                    }
                    else
                    {
                        pixel_counter = 1;
                    }
                }
                else
                {
                    temp_support_points[0] = pixels[i-1];
                    temp_support_points[1] = pixels[i];
                    lines[line_num].support_points.push_back(temp_support_points);
                }
            }
        }
            
        if (pixel_counter > detection_threshold)
        {
            vector<int> temp_endpoints(2,0);
            temp_endpoints[0] = line_start; 
            temp_endpoints[1] = pixels[pixels.size()-1];
            lines[line_num].endpoints.push_back(temp_endpoints);
			temp_endpoint_ranges[0] = current_scan[line_start];
			temp_endpoint_ranges[1] = current_scan[pixels[pixels.size()-1]];
			lines[line_num].endpoint_ranges.push_back(temp_endpoint_ranges);
		    for (unsigned int i=line_start_index;i<pixels.size(); i++)
            {
                lines[line_num].line_pixels.push_back(lines[line_num].pixels[i]);
            }
            
            
            if (current_scan[pixels[pixels.size()-1]] > max_range_threshold || pixels[pixels.size()-1] > tot_scans - out_of_view)
            {
                max_range_line = true;
            }
            lines[line_num].partial_observe.push_back(max_range_line);
        }
    }
    
    

    for (unsigned int i=0; i<lines.size(); i++)
    {
        for (unsigned int j=0; j<lines[i].endpoints.size(); j++)
        {
			if (j>0)
			{
	    		lines[i].lengths.push_back(abs(rho[lines[i].rho_index]* ( sin(phi[lines[i].endpoints[j][0]]-theta[lines[i].theta_index]) - sin(phi[lines[i].endpoints[j-1][1]]-theta[lines[i].theta_index]) ) ));
       	    }
            lines[i].lengths.push_back(abs(rho[lines[i].rho_index]* ( sin(phi[lines[i].endpoints[j][1]]-theta[lines[i].theta_index]) - sin(phi[lines[i].endpoints[j][0]]-theta[lines[i].theta_index]) ) ));
        }
    }

	/*
	vector<int> removable_lines;
	for (unsigned int i = 0; i<lines.size(); i++)
	{

		if (lines[i].line_pixels.size() == 0)
		{
			bool uniqueIndex = true;
				for (unsigned int k=0; k<removable_lines.size(); k++)
				{
					if ((unsigned int)removable_lines[k] == i)
					{
						uniqueIndex = false;
						break;
					}
				}
				if (uniqueIndex == true)
				{
					removable_lines.push_back(i);
				}
			continue;
		}
		for (unsigned int j = i+1; j<lines.size(); j++)
		{
			
			int num_common_pixels = 0;
			
			if (lines[i].line_pixels.size() == 0 || lines[j].line_pixels.size() == 0)
			{
				bool uniqueIndex = true;
				for (unsigned int k=0; k<removable_lines.size(); k++)
				{
					if ((unsigned int)removable_lines[k] == i)
					{
						uniqueIndex = false;
						break;
					}
				}
				if (uniqueIndex == true)
				{
					removable_lines.push_back(j);
				}
				continue;
			}			

			unsigned int i_index = lines[i].line_pixels[0];
			unsigned int j_index = lines[j].line_pixels[0];
			while (i_index <lines[i].line_pixels.size() && j_index < lines[j].line_pixels.size())
			{
				if (lines[i].line_pixels[i_index] == lines[j].line_pixels[j_index])
				{
					num_common_pixels ++;
					i_index++;
					j_index++;
				}
				else {
				if (lines[i].line_pixels[i_index] > lines[j].line_pixels[j_index])
				{
					j_index++;
				}
				else {
					i_index++;
				}
				}
			}
			if (num_common_pixels > 0.5 * lines[i].line_pixels.size())
			{
				bool uniqueIndex = true;
				for (unsigned int k=0; k<removable_lines.size(); k++)
				{
					if ((unsigned int)removable_lines[k] == i)
					{
						uniqueIndex = false;
						break;
					}
				}
				if (uniqueIndex == true)
					removable_lines.push_back(i);
			}
			if (num_common_pixels > 0.5 * lines[j].line_pixels.size())
			{
				bool uniqueIndex = true;
				for (unsigned int k=0; k<removable_lines.size(); k++)
				{
					if ((unsigned int)removable_lines[k] == j)
					{
						uniqueIndex = false;
						break;
					}
				}
				if (uniqueIndex == true)
					removable_lines.push_back(j);
			}
			
		}
	}
	
	
	
	vector<line> lines_copy;

	for (unsigned int i=0; i<lines.size(); i++)
	{	
		
		bool removable = false;
		for (unsigned int j=0; j<removable_lines.size(); j++)
		{
			if (i == (unsigned int)removable_lines[j])
			{
				removable = true;
				break;
			}
		}
		if (removable == false)
		lines_copy.push_back(lines[i]);
		
	}
	lines.clear();
	lines = lines_copy;
	*/
	
	for (unsigned int i=0; i<lines.size(); i++){
	
		
		vector<float> result = line_tuner(lines, i, current_scan, theta, rho, phi);
		lines[i].est_rho = result[0];
		lines[i].est_theta = result[1];
		int new_theta_index = 0;
		for (int j=0; j<theta.size(); j++)
		{
			if (theta[j] > lines[i].est_theta)
			{
				new_theta_index = j;
				break;
			}
		}
		if (new_theta_index != 0)
		{
			lines[i].theta_index = new_theta_index;
		}
	}
	
	for (unsigned int i = 0; i < lines.size(); i++) {
		current_lines->push_back(lines[i]);
	}
	
	if (current_lines->size() > 0) {	

		for (unsigned int i = 0; i < lines.size(); i++) {
			cout << "Line " << i << ":  est_rho "<< lines[i].est_rho<<",  theta "<< 180/3.1415 *  lines[i].est_theta <<",  votes " << lines[i].votes<<endl;
		}

		vector<bool> trim;

		for (unsigned int i=0; i<current_scan.size(); i++) {trim.push_back(false);}

		vector<float> trimmed_scan = current_scan;

		for (unsigned int i = 0; i < lines.size(); i++) {
			for (unsigned int j = 0; j < lines[i].line_pixels.size(); j++) {
				trim[lines[i].line_pixels[j]] = true;
			}
		}

		for (unsigned int i = 0; i < current_scan.size(); i++) {
			if (trim[i] == false) {
				trimmed_scan[i] = 0.0;
			}
		}

	



	
		  

		geometry_msgs::Point32 temp2;
		geometry_msgs::Polygon temp3;
		art_lrf::Lines line_msg;
		vector<int> temp_theta_index;
		vector<float> temp_rho;
		vector<geometry_msgs::Polygon> temp_endpoints;
		vector<geometry_msgs::Polygon> temp_endpoint_ranges;
		vector<geometry_msgs::Polygon> temp_lengths;
		vector<geometry_msgs::Point32> temp_points;

		for (unsigned int i = 0; i < lines.size(); i++) {
			temp_theta_index.push_back(lines[i].theta_index);
			temp_rho.push_back(lines[i].est_rho);
			

			for (unsigned int j = 0; j < lines[i].endpoints.size(); j++) {
				temp2.x = lines[i].endpoints[j][0];
				temp2.y = lines[i].endpoints[j][1];
				temp_points.push_back(temp2);
			}
			temp3.points = temp_points;
			temp_endpoints.push_back(temp3);
			temp_points.clear();

			for (unsigned int k = 0; k < lines[i].lengths.size(); k++) {
				temp2.x = lines[i].lengths[k];
				temp_points.push_back(temp2);
			}		
			temp3.points = temp_points;
			temp_lengths.push_back(temp3);
			temp_points.clear();

			for (unsigned int p = 0; p < lines[i].endpoint_ranges.size(); p++) {
				temp2.x = lines[i].endpoint_ranges[p][0];
				temp2.y = lines[i].endpoint_ranges[p][1];
				temp_points.push_back(temp2);
			}
			
			temp3.points = temp_points;
			temp_endpoint_ranges.push_back(temp3);
			temp_points.clear();
			
		}

		line_msg.theta_index = temp_theta_index;
		line_msg.est_rho = temp_rho;
		line_msg.endpoints = temp_endpoints;
		line_msg.lengths = temp_lengths;
		line_msg.endpoint_ranges = temp_endpoint_ranges;
		pub_lines.publish(line_msg);
	
		old_lines = current_lines;
		firstRun = false;
	
	} else if (!firstRun)
	{

		for (unsigned int i = 0; i < old_lines->size(); i++) {
			cout << "Line " << i << ":  est_rho "<< old_lines->at(i).est_rho<<",  theta "<< 180/3.1415*theta[old_lines->at(i).theta_index]<<",  "<<endl;
		}

		vector<bool> trim;

		for (unsigned int i=0; i<current_scan.size(); i++) {trim.push_back(false);}

		vector<float> trimmed_scan = current_scan;

		for (unsigned int i = 0; i < old_lines->size(); i++) {
			for (unsigned int j = 0; j < old_lines->at(i).line_pixels.size(); j++) {
				trim[old_lines->at(i).line_pixels[j]] = true;
			}
		}

		for (unsigned int i = 0; i < current_scan.size(); i++) {
			if (trim[i] == false) {
				trimmed_scan[i] = 0.0;
			}
		}

	

		geometry_msgs::Point32 temp2;
		geometry_msgs::Polygon temp3;
		art_lrf::Lines line_msg;
		vector<int> temp_theta_index;
		vector<float> temp_rho;
		vector<geometry_msgs::Polygon> temp_endpoints;
		vector<geometry_msgs::Polygon> temp_endpoint_ranges;
		vector<geometry_msgs::Polygon> temp_lengths;
		vector<geometry_msgs::Point32> temp_points;

	
		for (unsigned int i = 0; i < old_lines->size(); i++) {
			temp_theta_index.push_back(old_lines->at(i).theta_index);
			temp_rho.push_back(old_lines->at(i).est_rho);

			for (unsigned int j = 0; j < old_lines->at(i).endpoints.size(); j++) {
				temp2.x = old_lines->at(i).endpoints[j][0];
				temp2.y = old_lines->at(i).endpoints[j][1];
				temp_points.push_back(temp2);
			}
			temp3.points = temp_points;
			temp_endpoints.push_back(temp3);
			temp_points.clear();

			for (unsigned int k = 0; k < old_lines->at(i).lengths.size(); k++) {
				temp2.x = old_lines->at(i).lengths[k];
				temp_points.push_back(temp2);
			}		
			temp3.points = temp_points;
			temp_lengths.push_back(temp3);
			temp_points.clear();

			for (unsigned int p = 0; p < old_lines->at(i).endpoint_ranges.size(); p++) {
				temp2.x = old_lines->at(i).endpoint_ranges[p][0];
				temp2.y = old_lines->at(i).endpoint_ranges[p][1];
				temp_points.push_back(temp2);
			}
			temp3.points = temp_points;
			temp_endpoint_ranges.push_back(temp3);
			temp_points.clear();
			
		}

		line_msg.theta_index = temp_theta_index;
		line_msg.est_rho = temp_rho;
		line_msg.endpoints = temp_endpoints;
		line_msg.lengths = temp_lengths;
		line_msg.endpoint_ranges = temp_endpoint_ranges;
		pub_lines.publish(line_msg);
		}
	}
};


int main (int argc, char** argv) {
	ros::init(argc, argv, "line_finder");
	ros::NodeHandle nh;
	
	line_finder lf(nh);


	ros::Rate loop_rate(2);
	while(ros::ok()) {
		ros::spinOnce();
		loop_rate.sleep();
	}
	return 0;
}


















