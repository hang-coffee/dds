#include <math.h>
#define _MATH_EPS 1e-15

static double reduce_angle(double x) {
	while(x>M_2_PI) x-=M_2_PI;
	while(x<-M_2_PI) x+=M_2_PI;
	int q=0;
	if(x<0.0) {
		x=-x;
		q=2;
	}
	if(x>M_PI) {
		x=M_2_PI-x;
		q^=1;
	}
	if(x>M_PI_2) {
		x=M_PI-x;
		q^=1;
	}
	if(q<2) return x;
	else return -x;
}

double acos(double x) {
	if(x<-1.0 || x>1.0) return NAN;
	double y=sqrt(1.0-x*x);
	return atan2(y, x);
}

double asin(double x) {
	if(x<-1.0 || x>1.0) return NAN;
	if(x==1.0) return M_PI_2;
	if(x==-1.0) return -M_PI_2;
	return atan(x/sqrt(1.0-x*x));		// 三角恒等式
}

double atan(double x) {
	// 使用泰勒展开
	int sign; 
	double a;
	if(x<0.0) {
		sign=-1;
		a=-x;
	} else {
		sign=1;
		a=x;
	}
	double res;
	if(a>1.0) {
		// atan(a)=PI/2-atan(1/a)
		double inv=1.0/a;
		double term=inv;
		double sum=term;
		double x2=inv*inv;
		for(int i=1; i<40; i++) {
			// 泰勒求atan(1/a)
			term*=-x2;
			sum+=term/(double)(2*i+1);
			if(term<1e-18 && term>(-1e-18)) break;
		}
		res=M_PI*0.5-sum;
	} else {
		double term=a;
		double sum=term;
		double x2=a*a;
		for(int i=1; i<40; i++) {
			term*=-x2;
			sum+=term/(double)(2*i+1);
			if(term<1e-18 && term>(-1e-18)) break;
		}
		res=sum;
	}
	if(x<0.0) return -res;
	else return res;
}

double atan2(double y, double x) {
	if(x>0.0) return atan(y/x);
	if(x<0.0) {
		if(y>=0.0) return atan(y/x)+M_PI;
		else return atan(y/x)-M_PI;
	}
	if(y>0.0) return M_PI_2;
	if(y<0.0) return -M_PI_2;
	return NAN;
}

double cos(double x);
double cosh(double x);
double sin(double x) {
	
}
double sinh(double x);
double tanh(double x);
double exp(double x);
double frexp(double x, int *exponent);
double ldexp(double x, int exponent);
double log(double x);
double log10(double x);
double modf(double x, double *integer);
double pow(double x, double y);

double sqrt(double x) {
	// 使用牛顿迭代法进行平方根运算
	if(x==0.0) return 0.0;
	if(x<0.0) return NAN;
	double a=x;
	double b=(a+a/x)*0.5;
	while(1) {
		double diff;
		if(b>a) diff=b-a;
		else diff=a-b;
		if(diff<_MATH_EPS) break;
		a=b;
		b=(a+a/x)*0.5;
	}
	return b;
}

double ceil(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);
